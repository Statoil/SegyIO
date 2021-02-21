# segyio #

[![Travis](https://img.shields.io/travis/equinor/segyio/master.svg?label=travis)](https://travis-ci.org/equinor/segyio)
[![Appveyor](https://ci.appveyor.com/api/projects/status/2i5cr8ui2t9qbxk9?svg=true)](https://ci.appveyor.com/project/statoil-travis/segyio)
[![PyPI Updates](https://pyup.io/repos/github/equinor/segyio/shield.svg)](https://pyup.io/repos/github/equinor/segyio/)
[![Python 3](https://pyup.io/repos/github/equinor/segyio/python-3-shield.svg)](https://pyup.io/repos/github/equinor/segyio/)

## Documentation ##

The official documentation is hosted on [readthedocs](https://segyio.readthedocs.io/).

## Index ##

* [Introduction](#introduction)
* [Feature summary](#feature-summary)
* [Getting started](#getting-started)
    * [Quick start](#quick-start)
    * [Get segyio](#get-segyio)
    * [Build segyio](#build-segyio)
* [Tutorial](#tutorial)
    * [Basics](#basics)
    * [Modes](#modes)
    * [Mode examples](#mode-examples)
* [Goals](#project-goals)
* [Contributing](#contributing)
* [Examples](#examples)
* [Common issues](#common-issues)
* [History](#history)

## Introduction ##

Segyio is a small LGPL licensed C library for easy interaction with SEG-Y and
Seismic Unix formatted seismic data, with language bindings for Python and
Matlab. Segyio is an attempt to create an easy-to-use, embeddable,
community-oriented library for seismic applications. Features are added as they
are needed; suggestions and contributions of all kinds are very welcome.

To catch up on the latest development and features, see the
[changelog](changelog.md). To write future proof code, consult the planned
[breaking changes](breaking-changes.md).

## Feature summary ##

  * A low-level C interface with few assumptions; easy to bind to other
    languages
  * Read and write binary and textual headers
  * Read and write traces and trace headers
  * Simple, powerful, and native-feeling Python interface with numpy
    integration
  * Read and write seismic unix files
  * xarray integration with netcdf_segy
  * Some simple applications with unix philosophy

## Getting started ##

When segyio is built and installed, you're ready to start programming! Check
out the [tutorial](#tutorial), [examples](#examples), [example
programs](python/examples), and [example
notebooks](https://github.com/equinor/segyio-notebooks). For a technical
reference with examples and small recipes, [read the
docs](https://segyio.readthedocs.io/). API docs are also available with pydoc -
start your favourite Python interpreter and type `help(segyio)`, which should
integrate well with IDLE, pycharm and other Python tools.

### Quick start ###

Two examples on how to use segyio:

Integrate with numpy to filter for a given treshold value
```python
import segyio
import numpy as np
threshold = 1e-2
with segyio.open('file.sgy') as f:
    for trace in f.trace:
        filtered = trace[np.where(trace < threshold)]
```

Visualize the first inline slice in a seismic volume:

```python
import segyio
import matplotlib.pyplot as plt

filename = 'file.sgy'
with segyio.open(filename) as f:
    inline_number = f.ilines[0]
    inline_slice = f.iline[inline_number]
    plt.imshow(inline_slice.T)
    plt.show()
```

See the [examples](#examples) for more.

### Get segyio ###

A copy of segyio is available both as pre-built binaries and source code:

* In Debian [unstable](https://packages.debian.org/source/sid/segyio)
    * `apt install python3-segyio`
* Wheels for Python from [PyPI](https://pypi.python.org/pypi/segyio/)
    * `pip install segyio`
* Source code from [github](https://github.com/equinor/segyio)
    * `git clone https://github.com/statoil/segyio`
* Source code in [tarballs](https://github.com/equinor/segyio/releases)

### Build segyio ###

To build segyio you need:
 * A C99 compatible C compiler (tested mostly on gcc and clang)
 * A C++ compiler for the Python extension, and C++11 for the tests
 * [CMake](https://cmake.org/) version 2.8.12 or greater
 * [Python](https://www.python.org/) 3.6 or greater
 * [numpy](http://www.numpy.org/) version 1.10 or greater
 * [setuptools](https://pypi.python.org/pypi/setuptools) version 28 or greater
 * [setuptools-scm](https://pypi.python.org/pypi/setuptools_scm)
 * [pytest](https://pypi.org/project/pytest)

 To build the documentation, you also need
 [sphinx](https://pypi.org/project/Sphinx)

To build and install segyio, perform the following actions in your console:

```bash
git clone https://github.com/equinor/segyio
mkdir segyio/build
cd segyio/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
make
make install
```

`make install` must be done as root for a system install; if you want to
install in your home directory, add `-DCMAKE_INSTALL_PREFIX=~/` or some other
appropriate directory, or `make DESTDIR=~/ install`. Please ensure your
environment picks up on non-standard install locations (PYTHONPATH,
LD_LIBRARY_PATH and PATH).

If you have multiple Python installations, or want to use some alternative
interpreter, you can help cmake find the right one by passing
`-DPYTHON_EXECUTABLE=/opt/python/binary` along with install prefix and build
type.

To build the matlab bindings, invoke CMake with the option `-DBUILD_MEX=ON`. In
some environments the Matlab binaries are in a non-standard location, in which
case you need to help CMake find the matlab binaries by passing
`-DMATLAB_ROOT=/path/to/matlab`.

#### Developers ####

It's recommended to build in debug mode to get more warnings and to embed debug
symbols in the objects. Substituting `Debug` for `Release` in the
`CMAKE_BUILD_TYPE` is plenty.

Tests are located in the language/tests directories, and it's highly
recommended that new features added are demonstrated for correctness and
contract by adding a test. All tests can be run by invoking `ctest`. Feel free
to use the tests already written as a guide.

After building segyio you can run the tests with `ctest`, executed from the
build directory.

Please note that to run the Python examples you need to let your environment
know where to find the Python library. It can be installed as a user, or on
adding the segyio/build/python library to your pythonpath.

## Tutorial ##

All code in this tutorial assumes segyio is imported, and that numpy is
available as np.

```python
import segyio
import numpy as np
```

This tutorial assumes you're familiar with Python and numpy. For a refresh,
check out the [python tutorial](https://docs.python.org/3/tutorial/) and [numpy
quickstart](https://docs.scipy.org/doc/numpy-dev/user/quickstart.html)

### Basics ###

Opening a file for reading is done with the `segyio.open` function, and
idiomatically used with context managers. Using the `with` statement, files are
properly closed even in the case of exceptions. By default, files are opened
read-only.

```python
with segyio.open(filename) as f:
    ...
```

Open accepts several options (for more a more comprehensive reference, check
the open function's docstring with `help(segyio.open)`. The most important
option is the second (optional) positional argument. To open a file for
writing, do `segyio.open(filename, 'r+')`, from the C `fopen` function.

Files can be opened in *unstructured* mode, either by passing `segyio.open` the
optional arguments `strict=False`, in which case not establishing structure
(inline numbers, crossline numbers etc.) is not an error, and
`ignore_geometry=True`, in which case segyio won't even try to set these
internal attributes.

The segy file object has several public attributes describing this structure:
* `f.ilines`
    Inferred inline numbers
* `f.xlines`
    Inferred crossline numbers
* `f.offsets`
    Inferred offsets numbers
* `f.samples`
    Inferred sample offsets (frequency and recording time delay)
* `f.unstructured`
    True if unstructured, False if structured
* `f.ext_headers`
    The number of extended textual headers

If the file is opened *unstructured*, all the line properties will will be
`None`.

### Modes ###

In segyio, data is retrieved and written through so-called *modes*. Modes are
abstract arrays, or addressing schemes, and change what names and indices mean.
All modes are properties on the file handle object, support the `len` function,
and reads and writes are done through `f.mode[]`. Writes are done with
assignment. Modes support array slicing inspired by numpy. The following modes
are available:

* `trace`

    The trace mode offers raw addressing of traces as they are laid out in the
    file. This, along with `header`, is the only mode available for
    unstructured files. Traces are enumerated `0..len(f.trace)`.

    Reading a trace yields a numpy `ndarray`, and reading multiple traces
    yields a generator of `ndarray`s. Generator semantics are used and the same
    object is reused, so if you want to cache or address trace data later, you
    must explicitly copy.

    ```python
    >>> f.trace[10]
    >>> f.trace[-2]
    >>> f.trace[15:45]
    >>> f.trace[:45:3]
    ```

* `header`

    With addressing behaviour similar to `trace`, accessing items yield header
    objects instead of numpy `ndarray`s. Headers are dict-like objects, where
    keys are integers, seismic unix-style keys (in segyio.su module) and segyio
    enums (segyio.TraceField).

    Header values can be updated by assigning a dict-like to it, and keys not
    present on the right-hand-side of the assignment are *unmodified*.

    ```python
    >>> f.header[5] = { segyio.su.tracl: 10 }
    >>> f.header[5].items()
    >>> f.header[5][25, 37] # read multiple values at once
    ```

* `iline`, `xline`

    These modes will raise an error if the file is unstructured. They consider
    arguments to `[]` as the *keys* of the respective lines. Line numbers are
    always increasing, but can have arbitrary, uneven spacing. The valid names
    can be found in the `ilines` and `xlines` properties.

    As with traces, getting one line yields an `ndarray`, and a slice of lines
    yields a generator of `ndarray`s. When using slices with a step, some
    intermediate items might be skipped if it is not matched by the step, i.e.
    doing `f.line[1:10:3]` on a file with lines `[1,2,3,4,5]` is equivalent of
    looking up `1, 4, 7`, and finding `[1,4]`.

    When working with a 4D pre-stack file, the first offset is implicitly read.
    To access a different or a range of offsets, use comma separated indices or
    ranges, as such: `f.iline[120, 4]`.

* `fast`, `slow`

    These are aliases for `iline` and `xline`, determined by how the traces are
    laid out. For inline sorted files, `fast` would yield `iline`.

* `depth_slice`

    The depth slice is a horizontal, file-wide cut at a depth. The yielded
    values are `ndarray`s and generators-of-arrays.

* `gather`

    The `gather` is the intersection of an inline and crossline, a vertical
    column of the survey, and unless a single offset is specified returns an
    offset x samples `ndarray`. In the presence of ranges, it returns a
    generator of such `ndarray`s.

* `text`

    The `text` mode is an array of the textual headers, where `text[0]` is the
    standard-mandated textual header, and `1..n` are the optional extended
    headers.

    The text headers are returned as 3200-byte string-like blobs (bytes in
    Python 3, str in Python 2), as it is in the file. The `segyio.tools.wrap`
    function can create a line-oriented version of this string.

* `bin`

    The values of the file-wide binary header with a dict-like interface.
    Behaves like the `header` mode, but without the indexing.

### Mode examples ###

```python
>>> for line in f.iline[:2430]:
...     print(np.average(line))

>>> for line in f.xline[2:10]:
...     print(line)

>>> for line in f.fast[::2]:
...     print(np.min(line))

>>> for factor, offset in enumerate(f.iline[10, :]):
...     offset *= factor
        print(offset)

>>> f.gather[200, 241, :].shape

>>> text = f.text[0]
>>> type(text)
<type 'bytes'> # 'str' in Python 2

>>> f.trace[10] = np.zeros(len(f.samples))
```

More examples and recipes can be found in the docstrings `help(segyio)` and the
[examples](#examples) section.

## Project goals ##

Segyio does not necessarily attempt to be the end-all of SEG-Y interactions;
rather, we aim to lower the barrier to interacting with SEG-Y files for
embedding, new applications or free-standing programs.

Additionally, the aim is not to support the full standard or all exotic (but
standard compliant) formatted files out there. Some assumptions are made, such
as:

 * All traces in a file are assumed to be of the same size

Currently, segyio supports:
 * Post-stack 3D volumes, sorted with respect to two header words (generally
   INLINE and CROSSLINE)
 * Pre-stack 4D volumes, sorted with respect to three header words (generally
   INLINE, CROSSLINE, and OFFSET)
 * Unstructured data, i.e. a collection of traces
 * Most numerical formats (including IEEE 4- and 8-byte float, IBM float, 2-
   and 4-byte integers)

The writing functionality in segyio is largely meant to *modify* or adapt
files. A file created from scratch is not necessarily a to-spec SEG-Y file, as
we only necessarily write the header fields segyio needs to make sense of the
geometry. It is still highly recommended that SEG-Y files are maintained and
written according to specification, but segyio **does not** enforce this.


### SEG-Y Revisions ###

Segyio can handle a lot of files that are SEG-Y-like, i.e. segyio handles files
that don't strictly conform to the SEG-Y standard. Segyio also does not
discriminate between the revisions, but instead tries to use information
available in the file. For an *actual* standard's reference, please see the
publications by SEG:

- [SEG-Y 0 (1975)](https://seg.org/Portals/0/SEG/News%20and%20Resources/Technical%20Standards/seg_y_rev0.pdf)
- [SEG-Y 1 (2002)](https://seg.org/Portals/0/SEG/News%20and%20Resources/Technical%20Standards/seg_y_rev1.pdf)
- [SEG-Y 2 (2017)](https://seg.org/Portals/0/SEG/News%20and%20Resources/Technical%20Standards/seg_y_rev2_0-mar2017.pdf)

## Contributing ##

We welcome all kinds of contributions, including code, bug reports, issues,
feature requests, and documentation. The preferred way of submitting a
contribution is to either make an
[issue](https://github.com/equinor/segyio/issues) on github or by forking the
project on github and making a pull request.

## xarray integration ##

[Alan Richardson](https://github.com/ar4) has written a great little tool for
using [xarray](http://xarray.pydata.org/en/stable/) with segy files, which he
demos in this
[notebook](https://github.com/ar4/netcdf_segy/blob/master/notebooks/netcdf_segy.ipynb)

## Reproducing the test data ##

Small SEG-Y formatted files are included in the repository for test purposes.
The data is non-sensical and made to be predictable, and it is reproducible by
using segyio. The tests file are located in the test-data directory. To
reproduce the data file, build segyio and run the test program `make-file.py`,
`make-ps-file.py`, and `make-rotated-copies.py` as such:

```python
python examples/make-file.py small.sgy 50 1 6 20 25
python examples/make-ps-file.py small-ps.sgy 10 1 5 1 4 1 3
python examples/make-rotated-copies.py small.sgy
```

The small-lsb.sgy file was created by running the flip-endianness program. This
program is included in the segyio source tree, but not a part of the package,
and not intended for distribution and installation, only for reproducing test
files.

The seismic unix file small.su and small-lsb.su were created by the following
commands:

```bash
segyread tape=small.sgy ns=50 remap=tracr,cdp byte=189l,193l conv=1 format=1 \
         > small-lsb.su
suswapbytes < small.su > small-lsb.su
```

If you have have small data files with a free license, feel free to submit it
to the project!

## Examples ##

### Python ###

Import useful libraries:

```python
import segyio
import numpy as np
from shutil import copyfile
```

Open segy file and inspect it:

```python
filename = 'name_of_your_file.sgy'
with segyio.open(filename) as segyfile:

    # Memory map file for faster reading (especially if file is big...)
    segyfile.mmap()

    # Print binary header info
    print(segyfile.bin)
    print(segyfile.bin[segyio.BinField.Traces])

    # Read headerword inline for trace 10
    print(segyfile.header[10][segyio.TraceField.INLINE_3D])

    # Print inline and crossline axis
    print(segyfile.xlines)
    print(segyfile.ilines)
```

Read post-stack data cube contained in segy file:

```python
# Read data along first xline
data = segyfile.xline[segyfile.xlines[1]]

# Read data along last iline
data = segyfile.iline[segyfile.ilines[-1]]

# Read data along 100th time slice
data = segyfile.depth_slice[100]

# Read data cube
data = segyio.tools.cube(filename)
```

Read pre-stack data cube contained in segy file:

```python
filename = 'name_of_your_prestack_file.sgy'
with segyio.open(filename) as segyfile:

    # Print offsets
    print(segyfile.offset)

    # Read data along first iline and offset 100:  data [nxl x nt]
    data = segyfile.iline[0, 100]

    # Read data along first iline and all offsets gath:  data [noff x nxl x nt]
    data = np.asarray([np.copy(x) for x in segyfile.iline[0:1, :]])

    # Read data along first 5 ilines and all offsets gath:  data [noff nil x nxl x nt]
    data = np.asarray([np.copy(x) for x in segyfile.iline[0:5, :]])

    # Read data along first xline and all offsets gath:  data [noff x nil x nt]
    data = np.asarray([np.copy(x) for x in segyfile.xline[0:1, :]])
```

Read and understand fairly 'unstructured' data (e.g., data sorted in common shot gathers):

```python
filename = 'name_of_your_prestack_file.sgy'
with segyio.open(filename, ignore_geometry=True) as segyfile:
    segyfile.mmap()

    # Extract header word for all traces
    sourceX = segyfile.attributes(segyio.TraceField.SourceX)[:]

    # Scatter plot sources and receivers color-coded on their number
    plt.figure()
    sourceY = segyfile.attributes(segyio.TraceField.SourceY)[:]
    nsum = segyfile.attributes(segyio.TraceField.NSummedTraces)[:]
    plt.scatter(sourceX, sourceY, c=nsum, edgecolor='none')

    groupX = segyfile.attributes(segyio.TraceField.GroupX)[:]
    groupY = segyfile.attributes(segyio.TraceField.GroupY)[:]
    nstack = segyfile.attributes(segyio.TraceField.NStackedTraces)[:]
    plt.scatter(groupX, groupY, c=nstack, edgecolor='none')
```

Write segy file using same header of another file but multiply data by *2

```python
input_file = 'name_of_your_input_file.sgy'
output_file = 'name_of_your_output_file.sgy'

copyfile(input_file, output_file)

with segyio.open(output_file, "r+") as src:

    # multiply data by 2
    for i in src.ilines:
        src.iline[i] = 2 * src.iline[i]
```

[Make segy file from sctrach](python/examples/make-file.py)

### MATLAB ###

```
filename='name_of_your_file.sgy'

% Inspect segy
Segy_struct=SegySpec(filename,189,193,1);

% Read headerword inline for each trace
Segy.get_header(filename,'Inline3D')

%Read data along first xline
data= Segy.readCrossLine(Segy_struct,Segy_struct.crossline_indexes(1));

%Read cube
data=Segy.get_cube(Segy_struct);

%Write segy, use same header but multiply data by *2
input_file='input_file.sgy';
output_file='output_file.sgy';
copyfile(input_file,output_file)
data = Segy.get_traces(input_file);
data1 = 2*data;
Segy.put_traces(output_file, data1);
```

## Common issues ##

### Creating a new file is very slow, or copying headers is slow ###

Quite often issues show up where someone struggle with the performance of
segyio, in particular when creating new files. The culprit is often this code:

```
with segyio.create('new.sgy', spec) as dst:
    dst.header = headers
```

The code itself is perfectly ok, but it has subtle behaviour on some systems
when the file is newly created: it is performing many scattered writes to a
[sparse file](https://en.wikipedia.org/wiki/Sparse_file). This can be fast or
slow, largely depending on the [file
system](https://github.com/equinor/segyio/issues/423).

#### Possible solutions

Rewrite the loop to write to the file contiguously:
```
with segyio.create('new.sgy', spec) as dst:
    for i in range(spec.tracecount):
        dst.header[i] = headers[i]
        dst.trace[i] = traces[i]
```

If the file is modified copy of another file, without changing the trace
lengths, it's often faster (and easier!) to first copy the file without segyio,
and then use segyio to modify the copy in-place:

```
shutil.copyfile(srcfile, dstfile)
with segyio.open(dstfile) as f:
    f.header = headers
```

### ImportError: libsegyio.so.1: cannot open shared object file

This error shows up when the loader cannot find the core segyio library. If
you've explicitly set the install prefix (with `-DCMAKE_INSTALL_PREFIX`) you
must configure your loader to also look in this prefix, either with a
`ld.conf.d` file or the `LD_LIBRARY_PATH` variable.

If you haven't set `CMAKE_INSTALL_PREFIX`, cmake will by default install to
`/usr/local`, which your loader usually knows about. On Debian based systems,
the library often gets installed to `/usr/local/lib`, which the loader may not
know about. See [issue #239](https://github.com/equinor/segyio/issues/239).

#### Possible solutions

* Configure the loader (`sudo ldconfig` often does the trick)
* Install with a different, known prefix, e.g. `-DCMAKE_INSTALL_LIBDIR=lib64`

### RuntimeError: unable to find sorting

This exception is raised when segyio tries to open the in strict mode, under
the assumption that the file is a regular, sorted 3D volume. If the file is
just a collection of traces in arbitrary order, this would fail.

#### Possible solutions

Check if segyio.open `iline` and `xline` input parameters are correct for current file.
Segyio supports files that are just a collection of traces too, but has to be
told that it's ok to do so. Pass `strict = False` or `ignore_geometry = True`
to `segyio.open` to allow or force unstructured mode respectively. Please note
that `f.iline` and similar features are now disabled and will raise errors.

## History ##
Segyio was initially written and is maintained by [Equinor
ASA](http://www.equinor.com/) as a free, simple, easy-to-use way of interacting
with seismic data that can be tailored to our needs, and as contribution to the
free software community.
