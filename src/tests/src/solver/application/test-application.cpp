//
// Created by marechaljas on 24/11/23.
//

#define BOOST_TEST_MODULE application

#include <boost/test/unit_test.hpp>
#include <yuni/io/io.h>

BOOST_AUTO_TEST_CASE(test_yuni_absolute_path) {
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("/"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("/path"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("\\"), true); // backslash character not double backslash
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("\\path"), true);

    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("c:"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("0:"), false);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("c:/antares"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("8:/antares"), false);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("CC:/antares"), false);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("c:\\antares"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("c:antares"), false);

    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("."), false);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("/home/antares"), true);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("antares/plop"), false);
    BOOST_CHECK_EQUAL(Yuni::IO::IsAbsolute("antares\\plop"), false);
}