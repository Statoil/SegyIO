#if defined(_DEBUG) && defined(_MSC_VER)
#  define _CRT_NOFORCE_MAINFEST 1
#  undef _DEBUG
#  include <Python.h>
#  include <bytesobject.h>
#  define _DEBUG 1
#else
#  include <Python.h>
#  include <bytesobject.h>
#endif

#include "segyio/segy.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

namespace {

struct autofd {
    explicit autofd( segy_file* p = NULL ) : fd( p ) {}
    operator segy_file*() const;
    operator bool() const;

    segy_file* fd;
};

autofd::operator segy_file*() const {
    if( this->fd ) return this->fd;

    PyErr_SetString( PyExc_IOError, "I/O operation on closed file" );
    return NULL;
}


autofd::operator bool() const { return this->fd; }

struct segyiofd {
    PyObject_HEAD
    autofd fd;
};

PyObject* ValueError( const char* msg ) {
    PyErr_SetString( PyExc_ValueError, msg );
    return NULL;
}

PyObject* IndexError( const char* msg ) {
    PyErr_SetString( PyExc_IndexError, msg );
    return NULL;
}

namespace fd {

int init( segyiofd* self, PyObject* args, PyObject* ) {
    char *filename = NULL;
    char *mode = NULL;
    int mode_len = 0;
    if( !PyArg_ParseTuple( args, "ss#", &filename, &mode, &mode_len ) )
        return -1;

    if( mode_len == 0 ) {
        PyErr_SetString( PyExc_ValueError, "Mode string must be non-empty" );
        return -1;
    }

    if( mode_len > 3 ) {
        PyErr_Format( PyExc_ValueError, "Invalid mode string '%s'", mode );
        return -1;
    }

    /* init can be called multiple times, which is treated as opening a new
     * file on the same object. That means the previous file handle must be
     * properly closed before the new file is set
     */
    segy_file* fd = segy_open( filename, mode );

    if( !fd && !strstr( "rb" "wb" "ab" "r+b" "w+b" "a+b", mode ) ) {
        PyErr_Format( PyExc_ValueError, "Invalid mode string '%s'", mode );
        return -1;
    }

    if( !fd ) {
        PyErr_Format( PyExc_IOError, "Unable to open file '%s'", filename );
        return -1;
    }

    if( self->fd.fd ) segy_close( self->fd.fd );
    self->fd.fd = fd;

    return 0;
}

void dealloc( segyiofd* self ) {
    if( self->fd ) segy_close( self->fd.fd );
    Py_TYPE( self )->tp_free( (PyObject*) self );
}

PyObject* close( segyiofd* self ) {
    errno = 0;

    /* multiple close() is a no-op */
    if( !self->fd ) return Py_BuildValue( "" );

    segy_close( self->fd );
    self->fd.fd = NULL;

    if( errno ) return PyErr_SetFromErrno( PyExc_IOError );

    return Py_BuildValue( "" );
}

PyObject* flush( segyiofd* self ) {
    errno = 0;

    segy_file* fp = self->fd;
    if( !fp ) return NULL;

    segy_flush( self->fd, false );
    if( errno ) return PyErr_SetFromErrno( PyExc_IOError );

    return Py_BuildValue( "" );
}

PyObject* mmap( segyiofd* self ) {
    segy_file* fp = self->fd;

    if( !fp ) return NULL;

    const int err = segy_mmap( fp );

    if( err != SEGY_OK )
        Py_RETURN_FALSE;

    Py_RETURN_TRUE;
}

/*
 * No C++11, so no std::vector::data. single-alloc automatic heap buffer,
 * without resize
 */
struct heapbuffer {
    explicit heapbuffer( int sz ) : ptr( new( std::nothrow ) char[ sz ] ) {
        if( !this->ptr ) {
            PyErr_SetString( PyExc_MemoryError, "unable to alloc buffer" );
            return;
        }

        std::memset( this->ptr, 0, sz );
    }

    ~heapbuffer() { delete[] this->ptr; }

    operator char*()             { return this->ptr; }
    operator const char*() const { return this->ptr; }

    char* ptr;

private:
    heapbuffer( const heapbuffer& );
};

PyObject* gettext( segyiofd* self, PyObject* args ) {
    segy_file* fp = self->fd;
    if( !fp ) return NULL;

    int index = 0;
    if( !PyArg_ParseTuple(args, "i", &index ) ) return NULL;

    heapbuffer buffer( segy_textheader_size() );
    if( !buffer ) return NULL;

    const int error = index == 0
              ? segy_read_textheader( fp, buffer )
              : segy_read_ext_textheader( fp, index - 1, buffer );

    if( error != SEGY_OK )
        return PyErr_Format( PyExc_Exception,
                             "Could not read text header: %s", strerror(errno));

    const size_t len = std::strlen( buffer );
    return PyBytes_FromStringAndSize( buffer, len );
}

PyObject* puttext( segyiofd* self, PyObject* args ) {
    int index;
    char* buffer;
    int size;

    if( !PyArg_ParseTuple(args, "is#", &index, &buffer, &size ) )
        return NULL;

    segy_file* fp = self->fd;
    if( !fp ) return NULL;

    size = std::min( size, SEGY_TEXT_HEADER_SIZE );
    heapbuffer buf( SEGY_TEXT_HEADER_SIZE );
    if( !buf ) return NULL;
    std::copy( buffer, buffer + size, buf.ptr );

    const int err = segy_write_textheader( fp, index, buf );

    switch( err ) {
        case SEGY_OK:
            return Py_BuildValue("");

        case SEGY_FSEEK_ERROR:
        case SEGY_FWRITE_ERROR:
            return PyErr_SetFromErrno( PyExc_IOError );

        case SEGY_INVALID_ARGS:
            return IndexError( "text header index out of range" );

        default:
            return PyErr_Format( PyExc_RuntimeError,
                                 "unknown error code %d", err  );
    }
}

PyObject* getbin( segyiofd* self ) {
    segy_file* fp = self->fd;
    if( !fp ) return NULL;

    char buffer[ SEGY_BINARY_HEADER_SIZE ] = {};

    const int err = segy_binheader( fp, buffer );

    switch( err ) {
        case SEGY_OK:
            return PyBytes_FromStringAndSize( buffer, sizeof( buffer ) );

        case SEGY_FSEEK_ERROR:
        case SEGY_FREAD_ERROR:
            return PyErr_SetFromErrno( PyExc_IOError );

        default:
            return PyErr_Format( PyExc_RuntimeError,
                                 "unknown error code %d", err  );
    }
}


PyMethodDef methods [] = {
    { "close", (PyCFunction) fd::close, METH_VARARGS, "Close file." },
    { "flush", (PyCFunction) fd::flush, METH_VARARGS, "Flush file." },
    { "mmap",  (PyCFunction) fd::mmap,  METH_NOARGS,  "mmap file."  },

    { "gettext", (PyCFunction) fd::gettext, METH_VARARGS, "Get text header." },
    { "puttext", (PyCFunction) fd::puttext, METH_VARARGS, "Put text header." },

    { "getbin", (PyCFunction) fd::getbin, METH_NOARGS, "Get binary header." },

    { NULL }
};

}

PyTypeObject Segyiofd = {
    PyVarObject_HEAD_INIT( NULL, 0 )
    "_segyio.segyfd",               /* name */
    sizeof( segyiofd ),             /* basic size */
    0,                              /* tp_itemsize */
    (destructor)fd::dealloc,        /* tp_dealloc */
    0,                              /* tp_print */
    0,                              /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_compare */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    0,                              /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,             /* tp_flags */
    "segyio file descriptor",       /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    0,                              /* tp_iter */
    0,                              /* tp_iternext */
    fd::methods,                    /* tp_methods */
    0,                              /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    (initproc)fd::init,             /* tp_init */
};

}


static segy_file* get_FILE_pointer_from_capsule( PyObject* self ) {
    if( !self ) {
        PyErr_SetString( PyExc_TypeError, "The object was not of type FILE" );
        return NULL;
    }

    if( !PyObject_TypeCheck( self, &Segyiofd ) ) {
        PyErr_SetString( PyExc_TypeError, "The object was not of type FILE" );
        return NULL;
    }

    segy_file* fp = ((segyiofd*)self)->fd;
    if( !fp ) {
        PyErr_SetString( PyExc_IOError, "I/O operation invalid on closed file" );
        return NULL;
    }

    return fp;
}

// ------------- ERROR Handling -------------
struct error_args {
    int error;
    int errno_err;
    int field_1;
    int field_2;
    int field_count;
    const char *name;
};

static PyObject *py_handle_segy_error_(struct error_args args) {
    switch (args.error) {
        case SEGY_TRACE_SIZE_MISMATCH:
            return PyErr_Format(PyExc_RuntimeError,
                                "Number of traces is not consistent with file size. File may be corrupt.");

        case SEGY_INVALID_FIELD:
            if (args.field_count == 1) {
                return PyErr_Format(PyExc_IndexError, "Field value out of range: %d", args.field_1);
            } else {
                int inline_field = args.field_1;
                int crossline_field = args.field_2;
                return PyErr_Format(PyExc_IndexError, "Invalid inline (%d) or crossline (%d) field/byte offset. "
                        "Too large or between valid byte offsets.", inline_field, crossline_field);
            }
        case SEGY_INVALID_OFFSETS:
            return PyErr_Format(PyExc_RuntimeError, "Found more offsets than traces. File may be corrupt.");

        case SEGY_INVALID_SORTING:
            return PyErr_Format(PyExc_RuntimeError, "Unable to determine sorting. File may be corrupt.");

        case SEGY_INVALID_ARGS:
            return PyErr_Format(PyExc_RuntimeError, "Input arguments are invalid.");

        case SEGY_MISSING_LINE_INDEX:
            return PyErr_Format(PyExc_KeyError, "%s number %d does not exist.", args.name, args.field_1);

        default:
            errno = args.errno_err;
            return PyErr_SetFromErrno(PyExc_IOError);
    }
}

static PyObject *py_handle_segy_error(int error, int errno_err) {
    struct error_args args;
    args.error = error;
    args.errno_err = errno_err;
    args.field_1 = 0;
    args.field_2 = 0;
    args.field_count = 0;
    args.name = "";
    return py_handle_segy_error_(args);
}

static PyObject *py_handle_segy_error_with_fields(int error, int errno_err, int field_1, int field_2, int field_count) {
    struct error_args args;
    args.error = error;
    args.errno_err = errno_err;
    args.field_1 = field_1;
    args.field_2 = field_2;
    args.field_count = field_count;
    args.name = "";
    return py_handle_segy_error_(args);
}

static PyObject *py_handle_segy_error_with_index_and_name(int error, int errno_err, int index, const char *name) {
    struct error_args args;
    args.error = error;
    args.errno_err = errno_err;
    args.field_1 = index;
    args.field_2 = 0;
    args.field_count = 1;
    args.name = name;
    return py_handle_segy_error_(args);
}

// ------------ Text Header -------------

static PyObject *py_textheader_size(PyObject *self) {
    return Py_BuildValue("i", SEGY_TEXT_HEADER_SIZE);
}

// ------------ Binary and Trace Header ------------
static char *get_header_pointer_from_capsule(PyObject *capsule, int *length) {
    if( PyBytes_Check( capsule ) ) {
        Py_ssize_t len = PyBytes_Size( capsule );
        if( len < SEGY_BINARY_HEADER_SIZE ) {
            PyErr_SetString( PyExc_TypeError, "binary header too small" );
            return NULL;
        }

        if( length ) *length = len;
        return PyBytes_AsString( capsule );
    }

    if (PyCapsule_IsValid(capsule, "TraceHeader=char*")) {
        if (length) {
            *length = SEGY_TRACE_HEADER_SIZE;
        }
        return (char*)PyCapsule_GetPointer(capsule, "TraceHeader=char*");
    }
    PyErr_SetString(PyExc_TypeError, "The object was not a header type");
    return NULL;
}


static PyObject *py_get_field(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *header_capsule = NULL;
    int field;

    PyArg_ParseTuple(args, "Oi", &header_capsule, &field);

    int length = 0;
    char *header = get_header_pointer_from_capsule(header_capsule, &length);

    if (PyErr_Occurred()) { return NULL; }

    int value;
    int error;
    if (length == segy_binheader_size()) {
        error = segy_get_bfield(header, field, &value);
    } else {
        error = segy_get_field(header, field, &value);
    }

    if (error == 0) {
        return Py_BuildValue("i", value);
    } else {
        return py_handle_segy_error_with_fields(error, errno, field, 0, 1);
    }
}

static PyObject *py_set_field(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *header_capsule = NULL;
    int field;
    int value;

    PyArg_ParseTuple(args, "Oii", &header_capsule, &field, &value);

    int length = 0;
    char *header = get_header_pointer_from_capsule(header_capsule, &length);

    if (PyErr_Occurred()) { return NULL; }

    int error;
    if (length == segy_binheader_size()) {
        error = segy_set_bfield(header, field, value);
    } else {
        error = segy_set_field(header, field, value);
    }

    if (error == 0) {
        return Py_BuildValue("");
    } else {
        return py_handle_segy_error_with_fields(error, errno, field, 0, 1);
    }
}

// ------------ Binary Header -------------
static PyObject *py_binheader_size(PyObject *self) {
    return Py_BuildValue("i", segy_binheader_size());
}

static PyObject *py_empty_binaryhdr(PyObject *self) {
    char buffer[ SEGY_BINARY_HEADER_SIZE ] = {};
    return PyBytes_FromStringAndSize( buffer, sizeof( buffer ) );
}

static PyObject *py_write_binaryhdr(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *binary_header_capsule = NULL;

    PyArg_ParseTuple(args, "OO", &file_capsule, &binary_header_capsule);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);
    if (PyErr_Occurred()) { return NULL; }

    char *binary_header = get_header_pointer_from_capsule(binary_header_capsule, NULL);
    if (PyErr_Occurred()) { return NULL; }

    int error = segy_write_binheader(p_FILE, binary_header);

    if (error == 0) {
        return Py_BuildValue("");
    } else {
        return py_handle_segy_error(error, errno);
    }
}

// -------------- Trace Headers ----------
static char *get_trace_header_pointer_from_capsule(PyObject *capsule) {
    if (!PyCapsule_IsValid(capsule, "TraceHeader=char*")) {
        PyErr_Format(PyExc_TypeError, "The object was not of type TraceHeader.");
        return NULL;
    }
    return (char*)PyCapsule_GetPointer(capsule, "TraceHeader=char*");
}

static void *py_trace_header_destructor(PyObject *capsule) {
    char *trace_header = get_trace_header_pointer_from_capsule(capsule);
    free(trace_header);
    return NULL;
}

static PyObject *py_empty_trace_header(PyObject *self) {
    errno = 0;
    char *buffer = (char*)calloc(SEGY_TRACE_HEADER_SIZE, sizeof(char));
    return PyCapsule_New(buffer, "TraceHeader=char*", (PyCapsule_Destructor) py_trace_header_destructor);
}

static PyObject *py_read_trace_header(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    int traceno;
    PyObject *trace_header_capsule = NULL;
    long trace0;
    int trace_bsize;

    PyArg_ParseTuple(args, "OiOli", &file_capsule, &traceno, &trace_header_capsule, &trace0, &trace_bsize);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    char *buffer = get_trace_header_pointer_from_capsule(trace_header_capsule);

    if (PyErr_Occurred()) { return NULL; }

    int error = segy_traceheader(p_FILE, traceno, buffer, trace0, trace_bsize);

    if (error == 0) {
        Py_IncRef(trace_header_capsule);
        return trace_header_capsule;
    } else {
        return py_handle_segy_error(error, errno);
    }
}

static PyObject *py_write_trace_header(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    int traceno;
    PyObject *trace_header_capsule = NULL;
    long trace0;
    int trace_bsize;

    PyArg_ParseTuple(args, "OiOli", &file_capsule, &traceno, &trace_header_capsule, &trace0, &trace_bsize);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    char *buffer = get_trace_header_pointer_from_capsule(trace_header_capsule);

    if (PyErr_Occurred()) { return NULL; }

    int error = segy_write_traceheader(p_FILE, traceno, buffer, trace0, trace_bsize);

    if (error == 0) {
        return Py_BuildValue("");
    } else {
        return py_handle_segy_error(error, errno);
    }
}

static PyObject *py_field_forall(PyObject *self, PyObject *args ) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *buffer_out;
    int start, stop, step;
    long trace0;
    int trace_bsize;
    int field;

    PyArg_ParseTuple(args, "OOiiiili", &file_capsule,
                                       &buffer_out,
                                       &start,
                                       &stop,
                                       &step,
                                       &field,
                                       &trace0,
                                       &trace_bsize );

    segy_file* fp = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_out)) {
        PyErr_SetString(PyExc_TypeError, "The destination buffer is not of the correct type.");
        return NULL;
    }

    if(step == 0) {
        PyErr_SetString(PyExc_TypeError, "slice step cannot be zero");
        return NULL;
    }

    Py_buffer buffer;
    PyObject_GetBuffer(buffer_out, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    int error = segy_field_forall( fp,
                                   field,
                                   start,
                                   stop,
                                   step,
                                   (int*)buffer.buf,
                                   trace0,
                                   trace_bsize );

    int errorno = errno;

    PyBuffer_Release( &buffer );
    if( error != SEGY_OK ) {
        return py_handle_segy_error( error, errorno );
    }

    Py_IncRef(buffer_out);
    return buffer_out;
}

static PyObject *py_field_foreach(PyObject *self, PyObject *args ) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *buffer_out;
    PyObject *indices;
    int field;
    long trace0;
    int trace_bsize;

    PyArg_ParseTuple(args, "OOOili", &file_capsule,
                                     &buffer_out,
                                     &indices,
                                     &field,
                                     &trace0,
                                     &trace_bsize );

    segy_file* fp = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_out)) {
        PyErr_SetString(PyExc_TypeError, "The destination buffer is not of the correct type.");
        return NULL;
    }

    if (!PyObject_CheckBuffer(indices)) {
        PyErr_SetString(PyExc_TypeError, "The indices buffer is not of the correct type.");
        return NULL;
    }

    Py_buffer bufout;
    PyObject_GetBuffer(buffer_out, &bufout, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    Py_buffer bufindices;
    PyObject_GetBuffer(indices, &bufindices, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS);

    int len = bufindices.len / bufindices.itemsize;
    if( bufout.len / bufout.itemsize != len ) {
        PyErr_SetString(PyExc_ValueError, "Attributes array length != indices" );
        PyBuffer_Release( &bufout );
        PyBuffer_Release( &bufindices );
        return NULL;
    }

    const int* ind = (int*)bufindices.buf;
    int* out = (int*)bufout.buf;
    for( int i = 0; i < len; ++i ) {
        int err = segy_field_forall( fp, field,
                                     ind[ i ],
                                     ind[ i ] + 1,
                                     1,
                                     out + i,
                                     trace0,
                                     trace_bsize );

        if( err != SEGY_OK ) {
            PyBuffer_Release( &bufout );
            PyBuffer_Release( &bufindices );
            return py_handle_segy_error( err, errno );
        }
    }

    PyBuffer_Release( &bufout );
    PyBuffer_Release( &bufindices );

    Py_IncRef(buffer_out);
    return buffer_out;
}

static PyObject *py_trace_bsize(PyObject *self, PyObject *args) {
    errno = 0;
    int sample_count;

    PyArg_ParseTuple(args, "i", &sample_count);

    int byte_count = segy_trace_bsize(sample_count);

    return Py_BuildValue("i", byte_count);
}

static PyObject *py_get_dt(PyObject *self, PyObject *args) {
    errno = 0;

    PyObject *file_capsule = NULL;
    float fallback;
    PyArg_ParseTuple(args, "Of", &file_capsule, &fallback);
    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    float dt;
    int error = segy_sample_interval(p_FILE, fallback, &dt);
    if( error == 0 )
        return PyFloat_FromDouble( dt );

    if( error != SEGY_FREAD_ERROR )
        return py_handle_segy_error( error, errno );

    /*
     * Figure out if the problem is reading the trace header
     * or the binary header
     */
    char buffer[ SEGY_BINARY_HEADER_SIZE ];
    error = segy_binheader( p_FILE, buffer );
    if( error != 0 )
        return PyErr_Format( PyExc_RuntimeError,
                             "Error reading global binary header" );

    const long trace0 = segy_trace0( buffer );
    const int samples = segy_samples( buffer );
    const int trace_bsize = segy_trace_bsize( samples );
    error = segy_traceheader( p_FILE, 0, buffer, trace0, trace_bsize );
    if( error != 0 )
        return PyErr_Format( PyExc_RuntimeError,
                             "Error reading trace header (index 0)" );

    return py_handle_segy_error( error, errno );
}


static PyObject *py_init_line_metrics(PyObject *self, PyObject *args) {
    errno = 0;
    SEGY_SORTING sorting;
    int trace_count;
    int inline_count;
    int crossline_count;
    int offset_count;

    PyArg_ParseTuple(args, "iiiii", &sorting, &trace_count, &inline_count, &crossline_count, &offset_count);

    int iline_length = segy_inline_length(crossline_count);

    int xline_length = segy_crossline_length(inline_count);

    int iline_stride;
    int error = segy_inline_stride(sorting, inline_count, &iline_stride);
    //Only check first call since the only error that can occur is SEGY_INVALID_SORTING
    if( error ) { return py_handle_segy_error( error, errno ); }

    int xline_stride;
    segy_crossline_stride(sorting, crossline_count, &xline_stride);

    PyObject *dict = PyDict_New();
    PyDict_SetItemString(dict, "xline_length", Py_BuildValue("i", xline_length));
    PyDict_SetItemString(dict, "xline_stride", Py_BuildValue("i", xline_stride));
    PyDict_SetItemString(dict, "iline_length", Py_BuildValue("i", iline_length));
    PyDict_SetItemString(dict, "iline_stride", Py_BuildValue("i", iline_stride));

    return Py_BuildValue("O", dict);
}


static PyObject *py_init_metrics(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *binary_header_capsule = NULL;

    PyArg_ParseTuple(args, "OO", &file_capsule, &binary_header_capsule);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    char *binary_header = get_header_pointer_from_capsule(binary_header_capsule, NULL);

    if (PyErr_Occurred()) { return NULL; }

    long trace0 = segy_trace0(binary_header);
    int sample_count = segy_samples(binary_header);
    int format = segy_format(binary_header);
    int trace_bsize = segy_trace_bsize(sample_count);

    int trace_count;
    int error = segy_traces(p_FILE, &trace_count, trace0, trace_bsize);

    if (error != 0) {
        return py_handle_segy_error(error, errno);
    }

    PyObject *dict = PyDict_New();
    PyDict_SetItemString(dict, "trace0", Py_BuildValue("l", trace0));
    PyDict_SetItemString(dict, "sample_count", Py_BuildValue("i", sample_count));
    PyDict_SetItemString(dict, "format", Py_BuildValue("i", format));
    PyDict_SetItemString(dict, "trace_bsize", Py_BuildValue("i", trace_bsize));
    PyDict_SetItemString(dict, "trace_count", Py_BuildValue("i", trace_count));

    return Py_BuildValue("O", dict);
}

static PyObject *py_init_cube_metrics(PyObject *self, PyObject *args) {
    errno = 0;

    PyObject *file_capsule = NULL;
    int il_field;
    int xl_field;
    int trace_count;
    long trace0;
    int trace_bsize;

    PyArg_ParseTuple(args, "Oiiili", &file_capsule,
                                     &il_field,
                                     &xl_field,
                                     &trace_count,
                                     &trace0,
                                     &trace_bsize);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    int sorting;
    int error = segy_sorting(p_FILE, il_field,
                                     xl_field,
                                     SEGY_TR_OFFSET,
                                     &sorting,
                                     trace0, trace_bsize);

    if (error != 0) {
        return py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
    }

    int offset_count;
    error = segy_offsets(p_FILE, il_field, xl_field, trace_count, &offset_count, trace0, trace_bsize);

    if (error != 0) {
        return py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
    }

    int field;
    int xl_count;
    int il_count;
    int *l1out;
    int *l2out;

    if (sorting == SEGY_CROSSLINE_SORTING) {
        field = il_field;
        l1out = &xl_count;
        l2out = &il_count;
    } else if (sorting == SEGY_INLINE_SORTING) {
        field = xl_field;
        l1out = &il_count;
        l2out = &xl_count;
    } else {
        return PyErr_Format(PyExc_RuntimeError, "Unable to determine sorting. File may be corrupt.");
    }

    if( trace_count != offset_count ) {
        error = segy_count_lines(p_FILE, field, offset_count, l1out, l2out, trace0, trace_bsize);
    } else {
        il_count = xl_count = 1;
    }

    if (error != 0) {
        return py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
    }

    PyObject *dict = PyDict_New();
    PyDict_SetItemString(dict, "sorting",      Py_BuildValue("i", sorting));
    PyDict_SetItemString(dict, "iline_field",  Py_BuildValue("i", il_field));
    PyDict_SetItemString(dict, "xline_field",  Py_BuildValue("i", xl_field));
    PyDict_SetItemString(dict, "offset_field", Py_BuildValue("i", 37));
    PyDict_SetItemString(dict, "offset_count", Py_BuildValue("i", offset_count));
    PyDict_SetItemString(dict, "iline_count",  Py_BuildValue("i", il_count));
    PyDict_SetItemString(dict, "xline_count",  Py_BuildValue("i", xl_count));

    return Py_BuildValue("O", dict);
}

static Py_buffer check_and_get_buffer(PyObject *object, const char *name, unsigned int expected) {
    static const Py_buffer zero_buffer = { 0 };
    Py_buffer buffer = zero_buffer;
    if (!PyObject_CheckBuffer(object)) {
        PyErr_Format(PyExc_TypeError, "The destination for %s is not a buffer object", name);
        return zero_buffer;
    }
    PyObject_GetBuffer(object, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    if (strcmp(buffer.format, "i") != 0) {
        PyErr_Format(PyExc_TypeError, "The destination for %s is not a buffer object of type 'intc'", name);
        PyBuffer_Release(&buffer);
        return zero_buffer;
    }

    size_t buffer_len = (size_t)buffer.len;
    if (buffer_len < expected * sizeof(unsigned int)) {
        PyErr_Format(PyExc_ValueError, "The destination for %s is too small. ", name);
        PyBuffer_Release(&buffer);
        return zero_buffer;
    }

    return buffer;
}


static PyObject *py_init_indices(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *metrics = NULL;
    PyObject *iline_out = NULL;
    PyObject *xline_out = NULL;
    PyObject *offset_out = NULL;

    PyArg_ParseTuple(args, "OOOOO", &file_capsule, &metrics,
                            &iline_out, &xline_out, &offset_out);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyDict_Check(metrics)) {
        PyErr_SetString(PyExc_TypeError, "metrics is not a dictionary!");
        return NULL;
    }

    int iline_count;
    int xline_count;
    int offset_count;
    PyArg_Parse(PyDict_GetItemString(metrics, "iline_count"), "i", &iline_count);
    PyArg_Parse(PyDict_GetItemString(metrics, "xline_count"), "i", &xline_count);
    PyArg_Parse(PyDict_GetItemString(metrics, "offset_count"), "i", &offset_count);

    if (PyErr_Occurred()) { return NULL; }

    Py_buffer iline_buffer = check_and_get_buffer(iline_out, "inline", iline_count);

    if (PyErr_Occurred()) { return NULL; }

    Py_buffer xline_buffer = check_and_get_buffer(xline_out, "crossline", xline_count);

    if (PyErr_Occurred()) {
        PyBuffer_Release(&iline_buffer);
        return NULL;
    }

    Py_buffer offsets_buffer = check_and_get_buffer(offset_out, "offsets", offset_count);

    if (PyErr_Occurred()) {
        PyBuffer_Release(&iline_buffer);
        PyBuffer_Release(&xline_buffer);
        return NULL;
    }

    int il_field;
    int xl_field;
    int offset_field;
    int sorting;
    long trace0;
    int trace_bsize;

    PyArg_Parse(PyDict_GetItemString(metrics, "iline_field"), "i", &il_field);
    PyArg_Parse(PyDict_GetItemString(metrics, "xline_field"), "i", &xl_field);
    PyArg_Parse(PyDict_GetItemString(metrics, "offset_field"), "i", &offset_field);
    PyArg_Parse(PyDict_GetItemString(metrics, "sorting"), "i", &sorting);
    PyArg_Parse(PyDict_GetItemString(metrics, "trace0"), "l", &trace0);
    PyArg_Parse(PyDict_GetItemString(metrics, "trace_bsize"), "i", &trace_bsize);

    int error = segy_inline_indices(p_FILE, il_field, sorting, iline_count, xline_count, offset_count, (int*)iline_buffer.buf,
                                    trace0, trace_bsize);

    if (error != 0) {
        py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
        goto cleanup;
    }

    error = segy_crossline_indices(p_FILE, xl_field, sorting, iline_count, xline_count, offset_count, (int*)xline_buffer.buf,
                                   trace0, trace_bsize);

    if (error != 0) {
        py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
        goto cleanup;
    }

    error = segy_offset_indices( p_FILE, offset_field, offset_count,
                                 (int*)offsets_buffer.buf,
                                 trace0, trace_bsize );

    if (error != 0) {
        py_handle_segy_error_with_fields(error, errno, il_field, xl_field, 2);
        goto cleanup;
    }

    PyBuffer_Release(&offsets_buffer);
    PyBuffer_Release(&xline_buffer);
    PyBuffer_Release(&iline_buffer);
    return Py_BuildValue("");

cleanup:
    PyBuffer_Release(&offsets_buffer);
    PyBuffer_Release(&xline_buffer);
    PyBuffer_Release(&iline_buffer);
    return NULL;
}


static PyObject *py_fread_trace0(PyObject *self, PyObject *args) {
    errno = 0;
    int lineno;
    int other_line_length;
    int stride;
    int offsets;
    PyObject *indices_object;
    char *type_name;

    PyArg_ParseTuple(args, "iiiiOs", &lineno, &other_line_length, &stride, &offsets, &indices_object, &type_name);

    Py_buffer buffer;
    if (!PyObject_CheckBuffer(indices_object)) {
        PyErr_Format(PyExc_TypeError, "The destination for %s is not a buffer object", type_name);
        return NULL;
    }
    PyObject_GetBuffer(indices_object, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS);

    int trace_no;
    int linenos_sz = PyObject_Length(indices_object);
    int error = segy_line_trace0(lineno, other_line_length, stride, offsets, (int*)buffer.buf, linenos_sz, &trace_no);
    PyBuffer_Release( &buffer );

    if (error != 0) {
        return py_handle_segy_error_with_index_and_name(error, errno, lineno, type_name);
    }

    return Py_BuildValue("i", trace_no);
}

static PyObject *py_read_trace(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    PyObject *buffer_out;
    int start, length, step;
    long trace0;
    int trace_bsize;
    int format;
    int samples;

    PyArg_ParseTuple(args, "OOiiiiili", &file_capsule,
                                        &buffer_out,
                                        &start,
                                        &step,
                                        &length,
                                        &format,
                                        &samples,
                                        &trace0,
                                        &trace_bsize );

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_out)) {
        PyErr_SetString(PyExc_TypeError, "The destination buffer is not of the correct type.");
        return NULL;
    }
    Py_buffer buffer;
    PyObject_GetBuffer(buffer_out, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    int error = 0;
    float* buf = (float*)buffer.buf;
    Py_ssize_t i;

    for( i = 0; error == 0 && i < length; ++i, buf += samples ) {
        error = segy_readtrace(p_FILE, start + (i * step), buf, trace0, trace_bsize);
    }

    long long bufsize = (long long) length * samples;
    int conv_error = segy_to_native(format, bufsize, (float*)buffer.buf);
    PyBuffer_Release( &buffer );

    if (error != 0) {
        return py_handle_segy_error_with_index_and_name(error, errno, start + (i * step), "Trace");
    }

    if (conv_error != 0) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert buffer to native format.");
        return NULL;
    }

    Py_IncRef(buffer_out);
    return buffer_out;
}

static PyObject *py_write_trace(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    int trace_no;
    PyObject *buffer_in;
    long trace0;
    int trace_bsize;
    int format;
    int samples;

    PyArg_ParseTuple(args, "OiOliii", &file_capsule, &trace_no, &buffer_in, &trace0, &trace_bsize, &format, &samples);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_in)) {
        PyErr_SetString(PyExc_TypeError, "The source buffer is not of the correct type.");
        return NULL;
    }
    Py_buffer buffer;
    PyObject_GetBuffer(buffer_in, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    int error = segy_from_native(format, samples, (float*)buffer.buf);

    if (error != 0) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert buffer from native format.");
        PyBuffer_Release( &buffer );
        return NULL;
    }

    error = segy_writetrace(p_FILE, trace_no, (float*)buffer.buf, trace0, trace_bsize);
    int conv_error = segy_to_native(format, samples, (float*)buffer.buf);
    PyBuffer_Release( &buffer );

    if (error != 0) {
        return py_handle_segy_error_with_index_and_name(error, errno, trace_no, "Trace");
    }

    if (conv_error != 0) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert buffer to native format.");
        return NULL;
    }

    return Py_BuildValue("");
}

static PyObject *py_read_line(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    int line_trace0;
    int line_length;
    int stride;
    int offsets;
    PyObject *buffer_in;
    long trace0;
    int trace_bsize;
    int format;
    int samples;

    PyArg_ParseTuple(args, "OiiiiOliii", &file_capsule,
                                         &line_trace0,
                                         &line_length, &stride, &offsets,
                                         &buffer_in,
                                         &trace0, &trace_bsize,
                                         &format, &samples);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_in)) {
        PyErr_SetString(PyExc_TypeError, "The destination buffer is not of the correct type.");
        return NULL;
    }
    Py_buffer buffer;
    PyObject_GetBuffer(buffer_in, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    int error = segy_read_line(p_FILE, line_trace0, line_length, stride, offsets, (float*)buffer.buf, trace0, trace_bsize);

    if (error != 0) {
        PyBuffer_Release( &buffer );
        return py_handle_segy_error_with_index_and_name(error, errno, line_trace0, "Line");
    }

    error = segy_to_native(format, samples * line_length, (float*)buffer.buf);
    PyBuffer_Release( &buffer );

    if (error != 0) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert buffer to native format.");
        return NULL;
    }

    Py_IncRef(buffer_in);
    return buffer_in;
}

static PyObject *py_read_depth_slice(PyObject *self, PyObject *args) {
    errno = 0;
    PyObject *file_capsule = NULL;
    int depth;
    int count;
    int offsets;
    PyObject *buffer_out;
    long trace0;
    int trace_bsize;
    int format;
    int samples;

    PyArg_ParseTuple(args, "OiiiOliii", &file_capsule,
                                        &depth,
                                        &count,
                                        &offsets,
                                        &buffer_out,
                                        &trace0, &trace_bsize,
                                        &format, &samples);

    segy_file *p_FILE = get_FILE_pointer_from_capsule(file_capsule);

    if (PyErr_Occurred()) { return NULL; }

    if (!PyObject_CheckBuffer(buffer_out)) {
        PyErr_SetString(PyExc_TypeError, "The destination buffer is not of the correct type.");
        return NULL;
    }
    Py_buffer buffer;
    PyObject_GetBuffer(buffer_out, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE);

    Py_ssize_t trace_no = 0;
    int error = 0;
    float* buf = (float*)buffer.buf;

    for(trace_no = 0; error == 0 && trace_no < count; ++trace_no) {
        error = segy_readsubtr(p_FILE,
                               trace_no * offsets,
                               depth,
                               depth + 1,
                               1,
                               buf++,
                               NULL,
                               trace0, trace_bsize);
    }

    if (error != 0) {
        PyBuffer_Release( &buffer );
        return py_handle_segy_error_with_index_and_name(error, errno, trace_no, "Depth");
    }

    error = segy_to_native(format, count, (float*)buffer.buf);
    PyBuffer_Release( &buffer );

    if (error != 0) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert buffer to native format.");
        return NULL;
    }

    Py_IncRef(buffer_out);
    return buffer_out;
}

static PyObject * py_format(PyObject *self, PyObject *args) {
    PyObject *out;
    int format;

    PyArg_ParseTuple( args, "Oi", &out, &format );

    Py_buffer buffer;
    PyObject_GetBuffer( out, &buffer,
                        PyBUF_FORMAT | PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE );

    int err = segy_to_native( format, buffer.len / buffer.itemsize, (float*)buffer.buf );

    PyBuffer_Release( &buffer );

    if( err != SEGY_OK ) {
        PyErr_SetString( PyExc_RuntimeError, "Unable to convert to native float." );
        return NULL;
    }

    Py_IncRef( out );
    return out;
}

static PyObject* py_rotation(PyObject *self, PyObject* args) {
    PyObject* file = NULL;
    int line_length;
    int stride;
    int offsets;
    PyObject* linenos;
    long trace0;
    int trace_bsize;

    PyArg_ParseTuple( args, "OiiiOli", &file,
                                       &line_length,
                                       &stride,
                                       &offsets,
                                       &linenos,
                                       &trace0,
                                       &trace_bsize );

    segy_file* fp = get_FILE_pointer_from_capsule( file );
    if( PyErr_Occurred() ) { return NULL; }

    if ( !PyObject_CheckBuffer( linenos ) ) {
        PyErr_SetString(PyExc_TypeError, "The linenos object is not a correct buffer object");
        return NULL;
    }

    Py_buffer buffer;
    PyObject_GetBuffer(linenos, &buffer, PyBUF_FORMAT | PyBUF_C_CONTIGUOUS);
    int linenos_sz = PyObject_Length( linenos );

    errno = 0;
    float rotation;
    int err = segy_rotation_cw( fp, line_length,
                                    stride,
                                    offsets,
                                    (const int*)buffer.buf,
                                    linenos_sz,
                                    &rotation,
                                    trace0,
                                    trace_bsize );
    int errn = errno;
    PyBuffer_Release( &buffer );

    if( err != 0 ) return py_handle_segy_error_with_index_and_name( err, errn, 0, "Inline" );
    return PyFloat_FromDouble( rotation );
}


/*  define functions in module */
static PyMethodDef SegyMethods[] = {
        {"binheader_size",     (PyCFunction) py_binheader_size,     METH_NOARGS,  "Return the size of the binary header."},
        {"textheader_size",    (PyCFunction) py_textheader_size,    METH_NOARGS,  "Return the size of the text header."},

        {"empty_binaryheader", (PyCFunction) py_empty_binaryhdr,    METH_NOARGS,  "Create empty binary header for a segy file."},
        {"write_binaryheader", (PyCFunction) py_write_binaryhdr,    METH_VARARGS, "Write the binary header to a segy file."},

        {"empty_traceheader",  (PyCFunction) py_empty_trace_header, METH_NOARGS,  "Create empty trace header for a segy file."},
        {"read_traceheader",   (PyCFunction) py_read_trace_header,  METH_VARARGS, "Read a trace header from a segy file."},
        {"write_traceheader",  (PyCFunction) py_write_trace_header, METH_VARARGS, "Write a trace header to a segy file."},
        {"field_forall",       (PyCFunction) py_field_forall,       METH_VARARGS, "Read a single attribute from a set of headers."},
        {"field_foreach",      (PyCFunction) py_field_foreach,      METH_VARARGS, "Read a single attribute from a set of headers, given by a list of indices."},

        {"trace_bsize",        (PyCFunction) py_trace_bsize,        METH_VARARGS, "Returns the number of bytes in a trace."},
        {"get_field",          (PyCFunction) py_get_field,          METH_VARARGS, "Get a header field."},
        {"set_field",          (PyCFunction) py_set_field,          METH_VARARGS, "Set a header field."},

        {"init_line_metrics",  (PyCFunction) py_init_line_metrics,  METH_VARARGS, "Find the length and stride of inline and crossline."},
        {"init_cube_metrics",  (PyCFunction) py_init_cube_metrics,  METH_VARARGS, "Find the cube properties sorting, number of ilines, crosslines and offsets."},
        {"init_metrics",       (PyCFunction) py_init_metrics,       METH_VARARGS, "Find most metrics for a segy file."},
        {"init_indices",       (PyCFunction) py_init_indices,       METH_VARARGS, "Find the indices for inline, crossline and offsets."},
        {"fread_trace0",       (PyCFunction) py_fread_trace0,       METH_VARARGS, "Find trace0 of a line."},
        {"read_trace",         (PyCFunction) py_read_trace,         METH_VARARGS, "Read trace data."},
        {"write_trace",        (PyCFunction) py_write_trace,        METH_VARARGS, "Write trace data."},
        {"read_line",          (PyCFunction) py_read_line,          METH_VARARGS, "Read a xline/iline from file."},
        {"depth_slice",        (PyCFunction) py_read_depth_slice,   METH_VARARGS, "Read a depth slice."},
        {"get_dt",             (PyCFunction) py_get_dt,             METH_VARARGS, "Read dt from file."},
        {"native",             (PyCFunction) py_format,             METH_VARARGS, "Convert to native float."},
        {"rotation",           (PyCFunction) py_rotation,           METH_VARARGS, "Find survey clock-wise rotation in radians"},
        {NULL, NULL, 0, NULL}
};

/* module initialization */
#ifdef IS_PY3K
static struct PyModuleDef segyio_module = {
        PyModuleDef_HEAD_INIT,
        "_segyio",   /* name of module */
        NULL, /* module documentation, may be NULL */
        -1,  /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        SegyMethods
};

PyMODINIT_FUNC
PyInit__segyio(void) {

    Segyiofd.tp_new = PyType_GenericNew;
    if( PyType_Ready( &Segyiofd ) < 0 ) return NULL;

    PyObject* m = PyModule_Create(&segyio_module);

    if( !m ) return NULL;

    Py_INCREF( &Segyiofd );
    PyModule_AddObject( m, "segyiofd", (PyObject*)&Segyiofd );

    return m;
}
#else
PyMODINIT_FUNC
init_segyio(void) {
    Segyiofd.tp_new = PyType_GenericNew;
    if( PyType_Ready( &Segyiofd ) < 0 ) return;

    PyObject* m = Py_InitModule("_segyio", SegyMethods);

    Py_INCREF( &Segyiofd );
    PyModule_AddObject( m, "segyiofd", (PyObject*)&Segyiofd );
}
#endif
