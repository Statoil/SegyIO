#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

#include <catch/catch.hpp>

#include <segyio/segy.h>
#include <segyio/segyio.hpp>

#include "matchers.hpp"

SCENARIO( "constructing a filehandle", "[c++]" ) {
    GIVEN( "a closed filehandle" ) {

        sio::simple_file f;
        REQUIRE( !f.is_open() );

        WHEN( "closing the file" ) {
            f.close();

            THEN( "the file is closed" ) {
                CHECK( !f.is_open() );
            }
        }

        WHEN( "the filehandle is unchanged" ) {

                CHECK( !f.is_open() );
            THEN( "the size should be zero" ) {
                CHECK( f.size() == 0 );
            }

            THEN( "it can be copied" ) {
                auto g = f;
                CHECK( g.size() == f.size() );
            }

            THEN( "it can be moved-assigned" ) {
                auto g = std::move( f );
                CHECK( g.size() == 0 );

            }

            THEN( "it can be move-constructed" ) {
                sio::simple_file g( std::move( f ) );
                CHECK( g.size() == 0 );
            }

            THEN( "it can be return-val moved" ) {
                auto x = [] { 
                    return sio::simple_file{};
                } ();

                CHECK( x.size() == 0 );
            }
        }

        WHEN( "opening a non-existing wrong path" ) {
            sio::config c;
            THEN( "opening read-only fails" ) {
                CHECK_THROWS_AS( f.open( "garbage", c.readonly()),
                                 std::runtime_error );

                AND_THEN( "the file remains closed" )
                    CHECK( !f.is_open() );
            }

            THEN( "opening writable fails" ) {
                CHECK_THROWS_AS( f.open( "garbage", c.readwrite() ),
                                 std::runtime_error );

                AND_THEN( "the file remains closed" )
                    CHECK( !f.is_open() );
            }

            THEN( "opening truncating fails (on parse)" ) {
                // TODO: move-to tmpdir
                CHECK_THROWS_AS( f.open( "garbage", c.truncate() ),
                                 std::runtime_error );

                AND_THEN( "the file remains closed" )
                    CHECK( !f.is_open() );
            }
        }
    }
}

namespace {

struct tracegen {
    tracegen( double seed ) : n( seed - 0.00001 ) {}
    double operator()() { return this->n += 0.00001; };
    double n;
};

template< typename T >
std::vector< T > genrange( int len, T seed ) {
    std::vector< T > x( len );
    std::generate( x.begin(), x.end(), tracegen( seed ) );
    return x;
}

}

SCENARIO( "reading a single trace", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );

        WHEN( "the filehandle is unchanged" ) {
            THEN( "the file is open" ) {
                CHECK( f.is_open() );
            }

            THEN( "size matches tracecount" ) {
                CHECK( f.size() == 25 );
            }
        }

        WHEN( "closing the file" ) {
            f.close();

            THEN( "the filehandle should be closed" )
                CHECK( !f.is_open() );

            THEN( "the size should be zero" )
                CHECK( f.size() == 0 );
        }

        WHEN( "fetching a trace as default type" ) {
            const auto x = f.read(0);

            const auto expected = genrange( x.size(), 1.2 );
            THEN( "the data should be correct" ) {
                CHECK_THAT( x, ApproxRange( expected ) );
            }
        }

        WHEN( "fetching a trace as float" ) {
            // inline 2, crossline 21
            const auto x = f.read< float >( 6 );

            const auto expected = genrange( x.size(), 2.21f );
            THEN( "the data should be correct" ) {
                CHECK_THAT( x, ApproxRange( expected ) );
            }
        }

        WHEN( "reading into a too small vector" ) {
            std::vector< double > x( 10 );
            f.read( 6, x );

            THEN( "the buffer should be resized" ) {
                CHECK( x.size() == 50 );

                const auto expected = genrange( x.size(), 2.21 );
                AND_THEN( "the data should be correct" ) {
                    CHECK_THAT( x, ApproxRange( expected ) );
                }
            }
        }

        WHEN( "reading into an array" ) {
            std::array< float, 50 > x;
            f.read( 0, x.begin() );

            const auto expected = genrange( x.size(), 1.2f );
            THEN( "the data should be correct" ) {
                std::vector< float > xs( x.begin(), x.end() );
                CHECK_THAT( xs, ApproxRange( expected ) );
            }
        }

        WHEN( "reading into a back-insert'd vector" ) {
            std::vector< double > x;
            REQUIRE( x.empty() );

            f.read( 0, std::back_inserter( x ) );

            THEN( "the resulting vector should be resized" ) {
                CHECK( x.size() == 50 );

                AND_THEN( "the data should be correct" ) {
                    const auto expected = genrange( x.size(), 1.2 );
                    CHECK_THAT( x, ApproxRange( expected ) );
                }
            }
        }

        WHEN( "reading a trace outside the file" ) {
            THEN( "the read fails" ) {
                CHECK_THROWS_AS( f.read( f.size() ), std::out_of_range );

                AND_THEN( "size should be unchanged" )
                    CHECK( f.size() == 25 );

                AND_THEN( "file should still be open" )
                    CHECK( f.is_open() );

                AND_THEN( "a following valid read should be correct" ) {
                    const auto expected = genrange( 50, 1.2 );
                    CHECK_THAT( f.read( 0 ), ApproxRange( expected ) );
                }
            }
        }
    }

    GIVEN( "a closed file" ) {
        sio::simple_file f;

        WHEN( "reading a trace" ) {
            THEN( "the read should fail" ) {
                CHECK_THROWS_AS( f.read( 10 ), std::runtime_error );
            }
        }

        WHEN( "reading a trace out of range" ) {
            THEN( "the read should fail on being closed" ) {
                CHECK_THROWS_AS( f.read( 100 ), std::runtime_error );
            }
        }
    }
}

namespace {

std::string copyfile( const std::string& src, std::string dst ) {
    std::ifstream source( src, std::ios::binary );
    std::ofstream dest( dst, std::ios::binary | std::ios::trunc );
    dest << source.rdbuf();

    return dst;
}

}

SCENARIO( "writing a single trace", "[c++]" ) {
    GIVEN( "an open file" ) {
        auto filename = copyfile( "test-data/small.sgy",
                                  "c++-small-write-single.sgy" );

        sio::config c;
        sio::simple_file f( filename, c.readwrite() );

        WHEN( "filling the the first trace with zero" ) {
            std::vector< double > zeros( 50, 0 );
            f.put( 0, zeros );

            THEN( "getting it should produce zeros" ) {
                CHECK_THAT( f.read( 0 ), ApproxRange( zeros ) );
            }
        }

        WHEN( "writing a short trace" ) {
            auto orig = f.read( 0 );
            std::vector< double > zeros( 5, 0 );

            THEN( "the put should fail" ) {
                CHECK_THROWS_AS( f.put( 0, zeros ), std::length_error );

                AND_THEN( "the file should be unchanged" ) {
                    CHECK_THAT( f.read( 0 ), ApproxRange( orig ) );
                }
            }
        }

        WHEN( "writing a long trace" ) {
            auto orig = f.read( 0 );
            std::vector< double > zeros( 500, 0 );

            THEN( "the put should fail" ) {
                CHECK_THROWS_AS( f.put( 0, zeros ), std::length_error );

                AND_THEN( "the file should be unchanged" ) {
                    CHECK_THAT( f.read( 0 ), ApproxRange( orig ) );
                }
            }
        }

        WHEN( "reading a trace outside the file" ) {

            std::vector< double > zeros( 50, 0 );

            THEN( "the write fails" ) {
                CHECK_THROWS_AS( f.put( f.size(), zeros ), std::out_of_range );

                AND_THEN( "size should be unchanged" )
                    CHECK( f.size() == 25 );

                AND_THEN( "file should still be open" )
                    CHECK( f.is_open() );

                AND_THEN( "a following valid read should be correct" ) {
                    const auto expected = genrange( 50, 1.2 );
                    CHECK_THAT( f.read( 0 ), ApproxRange( expected ) );
                }
            }
        }
    }
}

SCENARIO( "reading a single inline", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );

        auto reference = [&f] {
            std::vector< double > reference( 50 * 5 );

            auto itr = reference.begin();
            for( int i = 0; i < 5; ++i )
                itr = f.read( i, itr );

            return reference;
        }();

        WHEN( "reading the first inline" ) {
            auto x = f.get_iline( 1 );

            THEN( "the data should be correct" ) {
                CHECK_THAT( x, ApproxRange( reference ) );
            }
        }

        WHEN( "reading into a vector" ) {
            std::vector< float > v;
            f.get_iline( 1, v );

            THEN( "the data should be correct" ) {
                std::vector< float > ref( reference.begin(), reference.end() );
                CHECK_THAT( v, ApproxRange( ref ) );
            }
        }

        WHEN( "reading into an iterator" ) {
            std::array< double, 5*50 > a;
            f.get_iline( 1, a.begin() );


            THEN( "the data should be correct" ) {
                std::vector< double > v( a.begin(), a.end() );
                CHECK_THAT( v, ApproxRange( reference ) );
            }
        }
    }
}

SCENARIO( "reading a single crossline", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );

        auto reference = [&f] {
            std::vector< double > reference( 50 * 5 );

            auto itr = reference.begin();
            for( int i = 0; i < 25; i += 5 )
                itr = f.read( i, itr );

            return reference;
        }();

        WHEN( "reading the first crossline" ) {
            auto x = f.get_xline( 20 );

            THEN( "the data should be correct" ) {
                CHECK_THAT( x, ApproxRange( reference ) );
            }
        }

        WHEN( "reading into a vector" ) {
            std::vector< float > v;
            f.get_xline( 20, v );

            THEN( "the data should be correct" ) {
                std::vector< float > ref( reference.begin(), reference.end() );
                CHECK_THAT( v, ApproxRange( ref ) );
            }
        }

        WHEN( "reading into an iterator" ) {
            std::array< double, 5*50 > a;
            f.get_xline( 20, a.begin() );


            THEN( "the data should be correct" ) {
                std::vector< double > v( a.begin(), a.end() );
                CHECK_THAT( v, ApproxRange( reference ) );
            }
        }
    }
}

SCENARIO( "reading a single traceheader ", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );
        const std::vector< int > reference = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 1, 20, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0 };
        WHEN( "reading the first traceheader" ) {
            sio::header h;
            h = f.get_traceheader( 0 );

            THEN( "the data should be correct" ) {
                CHECK( h.SEQ_LINE == reference[0] );
                CHECK( h.SEQ_FILE == reference[1] );
                CHECK( h.FIELD_RECORD == reference[2] );
                CHECK( h.NUMBER_ORIG_FIELD == reference[3] );
                CHECK( h.ENERGY_SOURCE_POINT == reference[4] );
                CHECK( h.ENSEMBLE == reference[5] );
                CHECK( h.NUM_IN_ENSEMBLE == reference[6] );
                CHECK( h.TRACE_ID == reference[7] );
                CHECK( h.SUMMED_TRACES == reference[8] );
                CHECK( h.STACKED_TRACES == reference[9] );
                CHECK( h.DATA_USE == reference[10] );
                CHECK( h.OFFSET == reference[11] );
                CHECK( h.RECV_GROUP_ELEV == reference[12] );
                CHECK( h.SOURCE_SURF_ELEV == reference[13] );
                CHECK( h.SOURCE_DEPTH == reference[14] );
                CHECK( h.RECV_DATUM_ELEV == reference[15] );
                CHECK( h.SOURCE_DATUM_ELEV == reference[16] );
                CHECK( h.SOURCE_WATER_DEPTH == reference[17] );
                CHECK( h.GROUP_WATER_DEPTH == reference[18] );
                CHECK( h.ELEV_SCALAR == reference[19] );
                CHECK( h.SOURCE_GROUP_SCALAR == reference[20] );
                CHECK( h.SOURCE_X == reference[21] );
                CHECK( h.SOURCE_Y == reference[22] );
                CHECK( h.GROUP_X == reference[23] );
                CHECK( h.GROUP_Y == reference[24] );
                CHECK( h.COORD_UNITS == reference[25] );
                CHECK( h.WEATHERING_VELO == reference[26] );
                CHECK( h.SUBWEATHERING_VELO == reference[27] );
                CHECK( h.SOURCE_UPHOLE_TIME == reference[28] );
                CHECK( h.GROUP_UPHOLE_TIME == reference[29] );
                CHECK( h.SOURCE_STATIC_CORR == reference[30] );
                CHECK( h.GROUP_STATIC_CORR == reference[31] );
                CHECK( h.TOT_STATIC_APPLIED == reference[32] );
                CHECK( h.LAG_A == reference[33] );
                CHECK( h.LAG_B == reference[34] );
                CHECK( h.DELAY_REC_TIME == reference[35] );
                CHECK( h.MUTE_TIME_START == reference[36] );
                CHECK( h.MUTE_TIME_END == reference[37] );
                CHECK( h.SAMPLE_COUNT == reference[38] );
                CHECK( h.SAMPLE_INTER == reference[39] );
                CHECK( h.GAIN_TYPE == reference[40] );
                CHECK( h.INSTR_GAIN_CONST == reference[41] );
                CHECK( h.INSTR_INIT_GAIN == reference[42] );
                CHECK( h.CORRELATED == reference[43] );
                CHECK( h.SWEEP_FREQ_START == reference[44] );
                CHECK( h.SWEEP_FREQ_END == reference[45] );
                CHECK( h.SWEEP_LENGTH == reference[46] );
                CHECK( h.SWEEP_TYPE == reference[47] );
                CHECK( h.SWEEP_TAPERLEN_START == reference[48] );
                CHECK( h.SWEEP_TAPERLEN_END == reference[49] );
                CHECK( h.TAPER_TYPE == reference[50] );
                CHECK( h.ALIAS_FILT_FREQ == reference[51] );
                CHECK( h.ALIAS_FILT_SLOPE == reference[52] );
                CHECK( h.NOTCH_FILT_FREQ == reference[53] );
                CHECK( h.NOTCH_FILT_SLOPE == reference[54] );
                CHECK( h.LOW_CUT_FREQ == reference[55] );
                CHECK( h.HIGH_CUT_FREQ == reference[56] );
                CHECK( h.LOW_CUT_SLOPE == reference[57] );
                CHECK( h.HIGH_CUT_SLOPE == reference[58] );
                CHECK( h.YEAR_DATA_REC == reference[59] );
                CHECK( h.DAY_OF_YEAR == reference[60] );
                CHECK( h.HOUR_OF_DAY == reference[61] );
                CHECK( h.MIN_OF_HOUR == reference[62] );
                CHECK( h.SEC_OF_MIN == reference[63] );
                CHECK( h.TIME_BASE_CODE == reference[64] );
                CHECK( h.WEIGHTING_FAC == reference[65] );
                CHECK( h.GEOPHONE_GROUP_ROLL1 == reference[66] );
                CHECK( h.GEOPHONE_GROUP_FIRST == reference[67] );
                CHECK( h.GEOPHONE_GROUP_LAST == reference[68] );
                CHECK( h.GAP_SIZE == reference[69] );
                CHECK( h.OVER_TRAVEL == reference[70] );
                CHECK( h.CDP_X == reference[71] );
                CHECK( h.CDP_Y == reference[72] );
                CHECK( h.INLINE == reference[73] );
                CHECK( h.CROSSLINE == reference[74] );
                CHECK( h.SHOT_POINT == reference[75] );
                CHECK( h.SHOT_POINT_SCALAR == reference[76] );
                CHECK( h.MEASURE_UNIT == reference[77] );
                CHECK( h.TRANSDUCTION_MANT == reference[78] );
                CHECK( h.TRANSDUCTION_EXP == reference[79] );
                CHECK( h.TRANSDUCTION_UNIT == reference[80] );
                CHECK( h.DEVICE_ID == reference[81] );
                CHECK( h.SCALAR_TRACE_HEADER == reference[82] );
                CHECK( h.SOURCE_TYPE == reference[83] );
                CHECK( h.SOURCE_ENERGY_DIR_MANT == reference[84] );
                CHECK( h.SOURCE_ENERGY_DIR_EXP == reference[85] );
                CHECK( h.SOURCE_MEASURE_MANT == reference[86] );
                CHECK( h.SOURCE_MEASURE_EXP == reference[87] );
                CHECK( h.SOURCE_MEASURE_UNIT == reference[88] );
                CHECK( h.UNASSIGNED1 == reference[89] );
                CHECK( h.UNASSIGNED2 == reference[90] );
            }
        }
    }
}

SCENARIO( "reading the first traceheader field for all traces", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );
        const std::vector< int > inlines = {
            1, 1, 1, 1, 1,
            2, 2, 2, 2, 2,
            3, 3, 3, 3, 3,
            4, 4, 4, 4, 4,
            5, 5, 5, 5, 5,
        };

        const std::vector< int > inlines_range = { 2, 3, 4, 5 };

        WHEN( "reading the traceheader field 1" ) {

            const int start = 0;
            const int stop  = 25;
            const int step  = 1;

            auto x = f.get_attributes( SEGY_TR_INLINE, start, stop, step );

            THEN( "the data should be correct" ){
               CHECK_THAT( x, ApproxRange( inlines ) );
            }
        }

        WHEN( "reading into a vector 1" ) {
            std::vector< int > v;

            const int start = 0;
            const int stop  = 25;
            const int step  = 1;

            f.get_attributes( SEGY_TR_INLINE, start, stop, step, v );

            THEN( "the data should be correct" ) {
                CHECK_THAT( v, ApproxRange( inlines ) );
            }
        }

        WHEN( "reading into an iterator" ) {
            std::array< int32_t, 25 > a;

            const int start = 0;
            const int stop  = 25;
            const int step  = 1;

            f.get_attributes( SEGY_TR_INLINE, start, stop, step, a.begin() );

            THEN( "the data should be correct" ) {
                std::vector< int32_t > v( a.begin(), a.end() );
                CHECK_THAT( v, ApproxRange( inlines ) );
            }
        }

        WHEN( "reading field from every 5-ft trace in a range" ) {

            const int start = 5;
            const int stop  = 21;
            const int step  = 5;

            auto x = f.get_attributes( SEGY_TR_INLINE, start, stop, step );

            THEN( "the data should be correct" ) {
                CHECK_THAT( x, ApproxRange( inlines_range ) );
            }
        }
    }
}

SCENARIO( "reading the sample interval ", "[c++]" ) {
    GIVEN( "an open file" ) {
        sio::simple_file f( "test-data/small.sgy" );
        float expected = 4000;

        WHEN( "reading the sample interval with fallback" ) {
            float x = f.get_dt( 0 );

            THEN( "the data should be correct" ) {
                CHECK( x == expected );
            }
        }

        WHEN( "reading the sample interval without fallback" ) {
            float x = f.get_dt();

            THEN( "the data should be correct" ) {
                CHECK( x == expected );
            }
        }
    }
}
