#include <gtest/gtest.h>
#include "testutils.h"
#include "streams.h"
#include <boost/filesystem.hpp>
#include "clientversion.h"

namespace TestBufferedFile {

    class TestBufferedFile : public ::testing::Test {};

    TEST(TestBufferedFile, streams_buffered_file)
    {
        FILE* file = fopen("streams_test_tmp", "w+b");
        // The value at each offset is the offset.
        for (uint8_t j = 0; j < 40; ++j) {
            fwrite(&j, 1, 1, file);
        }
        rewind(file);

        // The buffer size (second arg) must be greater than the rewind
        // amount (third arg).

        try {
            CBufferedFile bfbad(file, 25, 25, 222, 333);
            ASSERT_TRUE(false);
        } catch (const std::exception& e) {
            ASSERT_TRUE(strstr(e.what(),
                        "Rewind limit must be less than buffer size") != nullptr);
        }

        // The buffer is 25 bytes, allow rewinding 10 bytes.
        CBufferedFile bf(file, 25, 10, 222, 333);
        ASSERT_TRUE(!bf.eof());

        // These two members have no functional effect.
        ASSERT_EQ(bf.GetType(), 222);
        ASSERT_EQ(bf.GetVersion(), 333);

        uint8_t i;
        bf >> i;            // bf.nSrcPos = 15, bf.nReadPos = 1
        ASSERT_EQ(i, 0);
        bf >> i;            // bf.nSrcPos = 15, bf.nReadPos = 2
        ASSERT_EQ(i, 1);

        // After reading bytes 0 and 1, we're positioned at 2.
        ASSERT_EQ(bf.GetPos(), 2);

        // Rewind to offset 0, ok (within the 10 byte window).
        ASSERT_TRUE(bf.SetPos(0));  // bf.nSrcPos = 15, bf.nReadPos = 0
        bf >> i;                    // bf.nSrcPos = 15, bf.nReadPos = 1
        ASSERT_EQ(i, 0);

        // We can go forward to where we've been, but beyond may fail.
        ASSERT_TRUE(bf.SetPos(2));  // bf.nSrcPos = 15, bf.nReadPos = 2
        bf >> i;                    // bf.nSrcPos = 15, bf.nReadPos = 3
        ASSERT_EQ(i, 2);

        // If you know the maximum number of bytes that should be
        // read to deserialize the variable, you can limit the read
        // extent. The current file offset is 3, so the following
        // SetLimit() allows zero bytes to be read.
        ASSERT_TRUE(bf.SetLimit(3));
        try {
            bf >> i;
            ASSERT_TRUE(false);
        } catch (const std::exception& e) {
            ASSERT_TRUE(strstr(e.what(),
                            "Read attempted past buffer limit") != nullptr);
        }
        // The default argument removes the limit completely.
        ASSERT_TRUE(bf.SetLimit());
        // The read position should still be at 3 (no change).
        ASSERT_EQ(bf.GetPos(), 3);

        // Read from current offset, 3, forward until position 10.
        for (uint8_t j = 3; j < 10; ++j) {
            bf >> i;
            ASSERT_EQ(i, j);
        }
        ASSERT_EQ(bf.GetPos(), 10);

        // We're guaranteed (just barely) to be able to rewind to zero.
        ASSERT_TRUE(bf.SetPos(0));
        ASSERT_EQ(bf.GetPos(), 0);
        bf >> i;
        ASSERT_EQ(i, 0);

        // We can set the position forward again up to the farthest
        // into the stream we've been, but no farther. (Attempting
        // to go farther may succeed, but it's not guaranteed.)
        ASSERT_TRUE(bf.SetPos(10));
        bf >> i;
        ASSERT_EQ(i, 10);
        ASSERT_EQ(bf.GetPos(), 11);

        // Now it's only guaranteed that we can rewind to offset 1
        // (current read position, 11, minus rewind amount, 10).
        ASSERT_TRUE(bf.SetPos(1));
        ASSERT_EQ(bf.GetPos(), 1);
        bf >> i;
        ASSERT_EQ(i, 1);

        // We can stream into large variables, even larger than
        // the buffer size.
        ASSERT_TRUE(bf.SetPos(11));
        {
            uint8_t a[40 - 11];
            bf >> FLATDATA(a);
            for (uint8_t j = 0; j < sizeof(a); ++j) {
                ASSERT_EQ(a[j], 11 + j);
            }
        }
        ASSERT_EQ(bf.GetPos(), 40);

        // We've read the entire file, the next read should throw.
        try {
            bf >> i;
            ASSERT_TRUE(false);
        } catch (const std::exception& e) {
            ASSERT_TRUE(strstr(e.what(),
                            "CBufferedFile::Fill: end of file") != nullptr);
        }
        // Attempting to read beyond the end sets the EOF indicator.
        ASSERT_TRUE(bf.eof());

        // Still at offset 40, we can go back 10, to 30.
        ASSERT_EQ(bf.GetPos(), 40);
        ASSERT_TRUE(bf.SetPos(30));
        bf >> i;
        ASSERT_EQ(i, 30);
        ASSERT_EQ(bf.GetPos(), 31);

        // We're too far to rewind to position zero.
        ASSERT_TRUE(!bf.SetPos(0));
        // But we should now be positioned at least as far back as allowed
        // by the rewind window (relative to our farthest read position, 40).
        ASSERT_TRUE(bf.GetPos() <= 30);

        // We can explicitly close the file, or the destructor will do it.
        bf.fclose();

        boost::filesystem::remove("streams_test_tmp");
    }

    TEST(TestBufferedFile, streams_buffered_file_rand)
    {

        // Make this test deterministic.
        seed_insecure_rand(true);

        for (int rep = 0; rep < 500; ++rep) {
            FILE* file = fopen("streams_test_tmp", "w+b");
            size_t fileSize = GetRandInt(256);
            for (uint8_t i = 0; i < fileSize; ++i) {
                fwrite(&i, 1, 1, file);
            }
            rewind(file);

            size_t bufSize = GetRandInt(300) + 1;
            size_t rewindSize = GetRandInt(bufSize);
            CBufferedFile bf(file, bufSize, rewindSize, 222, 333);
            size_t currentPos = 0;
            size_t maxPos = 0;
            for (int step = 0; step < 100; ++step) {
                if (currentPos >= fileSize)
                    break;

                // We haven't read to the end of the file yet.
                ASSERT_TRUE(!bf.eof());
                ASSERT_EQ(bf.GetPos(), currentPos);

                // Pretend the file consists of a series of objects of varying
                // sizes; the boundaries of the objects can interact arbitrarily
                // with the CBufferFile's internal buffer. These first three
                // cases simulate objects of various sizes (1, 2, 5 bytes).
                switch (GetRandInt(5)) {
                case 0: {
                    uint8_t a[1];
                    if (currentPos + 1 > fileSize)
                        continue;
                    bf.SetLimit(currentPos + 1);
                    bf >> FLATDATA(a);
                    for (uint8_t i = 0; i < 1; ++i) {
                        ASSERT_EQ(a[i], currentPos);
                        currentPos++;
                    }
                    break;
                }
                case 1: {
                    uint8_t a[2];
                    if (currentPos + 2 > fileSize)
                        continue;
                    bf.SetLimit(currentPos + 2);
                    bf >> FLATDATA(a);
                    for (uint8_t i = 0; i < 2; ++i) {
                        ASSERT_EQ(a[i], currentPos);
                        currentPos++;
                    }
                    break;
                }
                case 2: {
                    uint8_t a[5];
                    if (currentPos + 5 > fileSize)
                        continue;
                    bf.SetLimit(currentPos + 5);
                    bf >> FLATDATA(a);
                    for (uint8_t i = 0; i < 5; ++i) {
                        ASSERT_EQ(a[i], currentPos);
                        currentPos++;
                    }
                    break;
                }
                case 3: {
                    // Find a byte value (that is at or ahead of the current position).
                    size_t find = currentPos + GetRandInt(8);
                    if (find >= fileSize)
                        find = fileSize - 1;
                    bf.FindByte(static_cast<char>(find));
                    // The value at each offset is the offset.
                    ASSERT_EQ(bf.GetPos(), find);
                    currentPos = find;

                    bf.SetLimit(currentPos + 1);
                    uint8_t i;
                    bf >> i;
                    ASSERT_EQ(i, currentPos);
                    currentPos++;
                    break;
                }
                case 4: {
                    size_t requestPos = GetRandInt(maxPos + 4);
                    bool okay = bf.SetPos(requestPos);
                    // The new position may differ from the requested position
                    // because we may not be able to rewind beyond the rewind
                    // window, and we may not be able to move forward beyond the
                    // farthest position we've reached so far.
                    currentPos = bf.GetPos();
                    ASSERT_EQ(okay, currentPos == requestPos);
                    // Check that we can position within the rewind window.
                    if (requestPos <= maxPos &&
                        maxPos > rewindSize &&
                        requestPos >= maxPos - rewindSize) {
                        // We requested a position within the rewind window.
                        ASSERT_TRUE(okay);
                    }
                    break;
                }
                }
                if (maxPos < currentPos)
                    maxPos = currentPos;
            }
        }
        boost::filesystem::remove("streams_test_tmp");

    }

    TEST(TestBufferedFile, squishy_block_load) {

        CBlock block;
        block.SetNull();

        FILE* fileIn = fopen("squishy_block_load_tmp", "w+b");

        // dd if=blk00000.dat bs=1 count=1700 2>/dev/null | xxd -i
        const uint8_t blockOnDisk[] = {
            0xf9, 0xee, 0xe4, 0x8d, 0x9c, 0x06, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3b, 0xa3, 0xed, 0xfd,
            0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
            0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32, 0x3a, 0x9f, 0xb8, 0xaa,
            0x4b, 0x1e, 0x5e, 0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x29, 0xab, 0x5f, 0x49, 0x0f, 0x0f, 0x0f, 0x20, 0x0b, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xfd, 0x40, 0x05, 0x00, 0x0d, 0x5b, 0xa7, 0xcd,
            0xa5, 0xd4, 0x73, 0x94, 0x72, 0x63, 0xbf, 0x19, 0x42, 0x85, 0x31, 0x71,
            0x79, 0xd2, 0xb0, 0xd3, 0x07, 0x11, 0x9c, 0x2e, 0x7c, 0xc4, 0xbd, 0x8a,
            0xc4, 0x56, 0xf0, 0x77, 0x4b, 0xd5, 0x2b, 0x0c, 0xd9, 0x24, 0x9b, 0xe9,
            0xd4, 0x07, 0x18, 0xb6, 0x39, 0x7a, 0x4c, 0x7b, 0xbd, 0x8f, 0x2b, 0x32,
            0x72, 0xfe, 0xd2, 0x82, 0x3c, 0xd2, 0xaf, 0x4b, 0xd1, 0x63, 0x22, 0x00,
            0xba, 0x4b, 0xf7, 0x96, 0x72, 0x7d, 0x63, 0x47, 0xb2, 0x25, 0xf6, 0x70,
            0xf2, 0x92, 0x34, 0x32, 0x74, 0xcc, 0x35, 0x09, 0x94, 0x66, 0xf5, 0xfb,
            0x5f, 0x0c, 0xd1, 0xc1, 0x05, 0x12, 0x1b, 0x28, 0x21, 0x3d, 0x15, 0xdb,
            0x2e, 0xd7, 0xbd, 0xba, 0x49, 0x0b, 0x4c, 0xed, 0xc6, 0x97, 0x42, 0xa5,
            0x7b, 0x7c, 0x25, 0xaf, 0x24, 0x48, 0x5e, 0x52, 0x3a, 0xad, 0xbb, 0x77,
            0xa0, 0x14, 0x4f, 0xc7, 0x6f, 0x79, 0xef, 0x73, 0xbd, 0x85, 0x30, 0xd4,
            0x2b, 0x9f, 0x3b, 0x9b, 0xed, 0x1c, 0x13, 0x5a, 0xd1, 0xfe, 0x15, 0x29,
            0x23, 0xfa, 0xfe, 0x98, 0xf9, 0x5f, 0x76, 0xf1, 0x61, 0x5e, 0x64, 0xc4,
            0xab, 0xb1, 0x13, 0x7f, 0x4c, 0x31, 0xb2, 0x18, 0xba, 0x27, 0x82, 0xbc,
            0x15, 0x53, 0x47, 0x88, 0xdd, 0xa2, 0xcc, 0x08, 0xa0, 0xee, 0x29, 0x87,
            0xc8, 0xb2, 0x7f, 0xf4, 0x1b, 0xd4, 0xe3, 0x1c, 0xd5, 0xfb, 0x56, 0x43,
            0xdf, 0xe8, 0x62, 0xc9, 0xa0, 0x2c, 0xa9, 0xf9, 0x0c, 0x8c, 0x51, 0xa6,
            0x67, 0x1d, 0x68, 0x1d, 0x04, 0xad, 0x47, 0xe4, 0xb5, 0x3b, 0x15, 0x18,
            0xd4, 0xbe, 0xfa, 0xfe, 0xfe, 0x8c, 0xad, 0xfb, 0x91, 0x2f, 0x3d, 0x03,
            0x05, 0x1b, 0x1e, 0xfb, 0xf1, 0xdf, 0xe3, 0x7b, 0x56, 0xe9, 0x3a, 0x74,
            0x1d, 0x8d, 0xfd, 0x80, 0xd5, 0x76, 0xca, 0x25, 0x0b, 0xee, 0x55, 0xfa,
            0xb1, 0x31, 0x1f, 0xc7, 0xb3, 0x25, 0x59, 0x77, 0x55, 0x8c, 0xdd, 0xa6,
            0xf7, 0xd6, 0xf8, 0x75, 0x30, 0x6e, 0x43, 0xa1, 0x44, 0x13, 0xfa, 0xcd,
            0xae, 0xd2, 0xf4, 0x60, 0x93, 0xe0, 0xef, 0x1e, 0x8f, 0x8a, 0x96, 0x3e,
            0x16, 0x32, 0xdc, 0xbe, 0xeb, 0xd8, 0xe4, 0x9f, 0xd1, 0x6b, 0x57, 0xd4,
            0x9b, 0x08, 0xf9, 0x76, 0x2d, 0xe8, 0x91, 0x57, 0xc6, 0x52, 0x33, 0xf6,
            0x0c, 0x8e, 0x38, 0xa1, 0xf5, 0x03, 0xa4, 0x8c, 0x55, 0x5f, 0x8e, 0xc4,
            0x5d, 0xed, 0xec, 0xd5, 0x74, 0xa3, 0x76, 0x01, 0x32, 0x3c, 0x27, 0xbe,
            0x59, 0x7b, 0x95, 0x63, 0x43, 0x10, 0x7f, 0x8b, 0xd8, 0x0f, 0x3a, 0x92,
            0x5a, 0xfa, 0xf3, 0x08, 0x11, 0xdf, 0x83, 0xc4, 0x02, 0x11, 0x6b, 0xb9,
            0xc1, 0xe5, 0x23, 0x1c, 0x70, 0xff, 0xf8, 0x99, 0xa7, 0xc8, 0x2f, 0x73,
            0xc9, 0x02, 0xba, 0x54, 0xda, 0x53, 0xcc, 0x45, 0x9b, 0x7b, 0xf1, 0x11,
            0x3d, 0xb6, 0x5c, 0xc8, 0xf6, 0x91, 0x4d, 0x36, 0x18, 0x56, 0x0e, 0xa6,
            0x9a, 0xbd, 0x13, 0x65, 0x8f, 0xa7, 0xb6, 0xaf, 0x92, 0xd3, 0x74, 0xd6,
            0xec, 0xa9, 0x52, 0x9f, 0x8b, 0xd5, 0x65, 0x16, 0x6e, 0x4f, 0xcb, 0xf2,
            0xa8, 0xdf, 0xb3, 0xc9, 0xb6, 0x95, 0x39, 0xd4, 0xd2, 0xee, 0x2e, 0x93,
            0x21, 0xb8, 0x5b, 0x33, 0x19, 0x25, 0xdf, 0x19, 0x59, 0x15, 0xf2, 0x75,
            0x76, 0x37, 0xc2, 0x80, 0x5e, 0x1d, 0x41, 0x31, 0xe1, 0xad, 0x9e, 0xf9,
            0xbc, 0x1b, 0xb1, 0xc7, 0x32, 0xd8, 0xdb, 0xa4, 0x73, 0x87, 0x16, 0xd3,
            0x51, 0xab, 0x30, 0xc9, 0x96, 0xc8, 0x65, 0x7b, 0xab, 0x39, 0x56, 0x7e,
            0xe3, 0xb2, 0x9c, 0x6d, 0x05, 0x4b, 0x71, 0x14, 0x95, 0xc0, 0xd5, 0x2e,
            0x1c, 0xd5, 0xd8, 0xe5, 0x5b, 0x4f, 0x0f, 0x03, 0x25, 0xb9, 0x73, 0x69,
            0x28, 0x07, 0x55, 0xb4, 0x6a, 0x02, 0xaf, 0xd5, 0x4b, 0xe4, 0xdd, 0xd9,
            0xf7, 0x7c, 0x22, 0x27, 0x2b, 0x8b, 0xbb, 0x17, 0xff, 0x51, 0x18, 0xfe,
            0xdb, 0xae, 0x25, 0x64, 0x52, 0x4e, 0x79, 0x7b, 0xd2, 0x8b, 0x5f, 0x74,
            0xf7, 0x07, 0x9d, 0x53, 0x2c, 0xcc, 0x05, 0x98, 0x07, 0x98, 0x9f, 0x94,
            0xd2, 0x67, 0xf4, 0x7e, 0x72, 0x4b, 0x3f, 0x1e, 0xcf, 0xe0, 0x0e, 0xc9,
            0xe6, 0x54, 0x1c, 0x96, 0x10, 0x80, 0xd8, 0x89, 0x12, 0x51, 0xb8, 0x4b,
            0x44, 0x80, 0xbc, 0x29, 0x2f, 0x6a, 0x18, 0x0b, 0xea, 0x08, 0x9f, 0xef,
            0x5b, 0xbd, 0xa5, 0x6e, 0x1e, 0x41, 0x39, 0x0d, 0x7c, 0x0e, 0x85, 0xba,
            0x0e, 0xf5, 0x30, 0xf7, 0x17, 0x74, 0x13, 0x48, 0x1a, 0x22, 0x64, 0x65,
            0xa3, 0x6e, 0xf6, 0xaf, 0xe1, 0xe2, 0xbc, 0xa6, 0x9d, 0x20, 0x78, 0x71,
            0x2b, 0x39, 0x12, 0xbb, 0xa1, 0xa9, 0x9b, 0x1f, 0xbf, 0xf0, 0xd3, 0x55,
            0xd6, 0xff, 0xe7, 0x26, 0xd2, 0xbb, 0x6f, 0xbc, 0x10, 0x3c, 0x4a, 0xc5,
            0x75, 0x6e, 0x5b, 0xee, 0x6e, 0x47, 0xe1, 0x74, 0x24, 0xeb, 0xcb, 0xf1,
            0xb6, 0x3d, 0x8c, 0xb9, 0x0c, 0xe2, 0xe4, 0x01, 0x98, 0xb4, 0xf4, 0x19,
            0x86, 0x89, 0xda, 0xea, 0x25, 0x43, 0x07, 0xe5, 0x2a, 0x25, 0x56, 0x2f,
            0x4c, 0x14, 0x55, 0x34, 0x0f, 0x0f, 0xfe, 0xb1, 0x0f, 0x9d, 0x8e, 0x91,
            0x47, 0x75, 0xe3, 0x7d, 0x0e, 0xdc, 0xa0, 0x19, 0xfb, 0x1b, 0x9c, 0x6e,
            0xf8, 0x12, 0x55, 0xed, 0x86, 0xbc, 0x51, 0xc5, 0x39, 0x1e, 0x05, 0x91,
            0x48, 0x0f, 0x66, 0xe2, 0xd8, 0x8c, 0x5f, 0x4f, 0xd7, 0x27, 0x76, 0x97,
            0x96, 0x86, 0x56, 0xa9, 0xb1, 0x13, 0xab, 0x97, 0xf8, 0x74, 0xfd, 0xd5,
            0xf2, 0x46, 0x5e, 0x55, 0x59, 0x53, 0x3e, 0x01, 0xba, 0x13, 0xef, 0x4a,
            0x8f, 0x7a, 0x21, 0xd0, 0x2c, 0x30, 0xc8, 0xde, 0xd6, 0x8e, 0x8c, 0x54,
            0x60, 0x3a, 0xb9, 0xc8, 0x08, 0x4e, 0xf6, 0xd9, 0xeb, 0x4e, 0x92, 0xc7,
            0x5b, 0x07, 0x85, 0x39, 0xe2, 0xae, 0x78, 0x6e, 0xba, 0xb6, 0xda, 0xb7,
            0x3a, 0x09, 0xe0, 0xaa, 0x9a, 0xc5, 0x75, 0xbc, 0xef, 0xb2, 0x9e, 0x93,
            0x0a, 0xe6, 0x56, 0xe5, 0x8b, 0xcb, 0x51, 0x3f, 0x7e, 0x3c, 0x17, 0xe0,
            0x79, 0xdc, 0xe4, 0xf0, 0x5b, 0x5d, 0xbc, 0x18, 0xc2, 0xa8, 0x72, 0xb2,
            0x25, 0x09, 0x74, 0x0e, 0xbe, 0x6a, 0x39, 0x03, 0xe0, 0x0a, 0xd1, 0xab,
            0xc5, 0x50, 0x76, 0x44, 0x18, 0x62, 0x64, 0x3f, 0x93, 0x60, 0x6e, 0x3d,
            0xc3, 0x5e, 0x8d, 0x9f, 0x2c, 0xae, 0xf3, 0xee, 0x6b, 0xe1, 0x4d, 0x51,
            0x3b, 0x2e, 0x06, 0x2b, 0x21, 0xd0, 0x06, 0x1d, 0xe3, 0xbd, 0x56, 0x88,
            0x17, 0x13, 0xa1, 0xa5, 0xc1, 0x7f, 0x5a, 0xce, 0x05, 0xe1, 0xec, 0x09,
            0xda, 0x53, 0xf9, 0x94, 0x42, 0xdf, 0x17, 0x5a, 0x49, 0xbd, 0x15, 0x4a,
            0xa9, 0x6e, 0x49, 0x49, 0xde, 0xcd, 0x52, 0xfe, 0xd7, 0x9c, 0xcf, 0x7c,
            0xcb, 0xce, 0x32, 0x94, 0x14, 0x19, 0xc3, 0x14, 0xe3, 0x74, 0xe4, 0xa3,
            0x96, 0xac, 0x55, 0x3e, 0x17, 0xb5, 0x34, 0x03, 0x36, 0xa1, 0xa2, 0x5c,
            0x22, 0xf9, 0xe4, 0x2a, 0x24, 0x3b, 0xa5, 0x40, 0x44, 0x50, 0xb6, 0x50,
            0xac, 0xfc, 0x82, 0x6a, 0x6e, 0x43, 0x29, 0x71, 0xac, 0xe7, 0x76, 0xe1,
            0x57, 0x19, 0x51, 0x5e, 0x16, 0x34, 0xce, 0xb9, 0xa4, 0xa3, 0x50, 0x61,
            0xb6, 0x68, 0xc7, 0x49, 0x98, 0xd3, 0xdf, 0xb5, 0x82, 0x7f, 0x62, 0x38,
            0xec, 0x01, 0x53, 0x77, 0xe6, 0xf9, 0xc9, 0x4f, 0x38, 0x10, 0x87, 0x68,
            0xcf, 0x6e, 0x5c, 0x8b, 0x13, 0x2e, 0x03, 0x03, 0xfb, 0x5a, 0x20, 0x03,
            0x68, 0xf8, 0x45, 0xad, 0x9d, 0x46, 0x34, 0x30, 0x35, 0xa6, 0xff, 0x94,
            0x03, 0x1d, 0xf8, 0xd8, 0x30, 0x94, 0x15, 0xbb, 0x3f, 0x6c, 0xd5, 0xed,
            0xe9, 0xc1, 0x35, 0xfd, 0xab, 0xcc, 0x03, 0x05, 0x99, 0x85, 0x8d, 0x80,
            0x3c, 0x0f, 0x85, 0xbe, 0x76, 0x61, 0xc8, 0x89, 0x84, 0xd8, 0x8f, 0xaa,
            0x3d, 0x26, 0xfb, 0x0e, 0x9a, 0xac, 0x00, 0x56, 0xa5, 0x3f, 0x1b, 0x5d,
            0x0b, 0xae, 0xd7, 0x13, 0xc8, 0x53, 0xc4, 0xa2, 0x72, 0x68, 0x69, 0xa0,
            0xa1, 0x24, 0xa8, 0xa5, 0xbb, 0xc0, 0xfc, 0x0e, 0xf8, 0x0c, 0x8a, 0xe4,
            0xcb, 0x53, 0x63, 0x6a, 0xa0, 0x25, 0x03, 0xb8, 0x6a, 0x1e, 0xb9, 0x83,
            0x6f, 0xcc, 0x25, 0x98, 0x23, 0xe2, 0x69, 0x2d, 0x92, 0x1d, 0x88, 0xe1,
            0xff, 0xc1, 0xe6, 0xcb, 0x2b, 0xde, 0x43, 0x93, 0x9c, 0xeb, 0x3f, 0x32,
            0xa6, 0x11, 0x68, 0x6f, 0x53, 0x9f, 0x8f, 0x7c, 0x9f, 0x0b, 0xf0, 0x03,
            0x81, 0xf7, 0x43, 0x60, 0x7d, 0x40, 0x96, 0x0f, 0x06, 0xd3, 0x47, 0xd1,
            0xcd, 0x8a, 0xc8, 0xa5, 0x19, 0x69, 0xc2, 0x5e, 0x37, 0x15, 0x0e, 0xfd,
            0xf7, 0xaa, 0x4c, 0x20, 0x37, 0xa2, 0xfd, 0x05, 0x16, 0xfb, 0x44, 0x45,
            0x25, 0xab, 0x15, 0x7a, 0x0e, 0xd0, 0xa7, 0x41, 0x2b, 0x2f, 0xa6, 0x9b,
            0x21, 0x7f, 0xe3, 0x97, 0x26, 0x31, 0x53, 0x78, 0x2c, 0x0f, 0x64, 0x35,
            0x1f, 0xbd, 0xf2, 0x67, 0x8f, 0xa0, 0xdc, 0x85, 0x69, 0x91, 0x2d, 0xcd,
            0x8e, 0x3c, 0xca, 0xd3, 0x8f, 0x34, 0xf2, 0x3b, 0xbb, 0xce, 0x14, 0xc6,
            0xa2, 0x6a, 0xc2, 0x49, 0x11, 0xb3, 0x08, 0xb8, 0x2c, 0x7e, 0x43, 0x06,
            0x2d, 0x18, 0x0b, 0xae, 0xac, 0x4b, 0xa7, 0x15, 0x38, 0x58, 0x36, 0x5c,
            0x72, 0xc6, 0x3d, 0xcf, 0x5f, 0x6a, 0x5b, 0x08, 0x07, 0x0b, 0x73, 0x0a,
            0xdb, 0x01, 0x7a, 0xea, 0xe9, 0x25, 0xb7, 0xd0, 0x43, 0x99, 0x79, 0xe2,
            0x67, 0x9f, 0x45, 0xed, 0x2f, 0x25, 0xa7, 0xed, 0xcf, 0xd2, 0xfb, 0x77,
            0xa8, 0x79, 0x46, 0x30, 0x28, 0x5c, 0xcb, 0x0a, 0x07, 0x1f, 0x5c, 0xce,
            0x41, 0x0b, 0x46, 0xdb, 0xf9, 0x75, 0x0b, 0x03, 0x54, 0xaa, 0xe8, 0xb6,
            0x55, 0x74, 0x50, 0x1c, 0xc6, 0x9e, 0xfb, 0x5b, 0x6a, 0x43, 0x44, 0x40,
            0x74, 0xfe, 0xe1, 0x16, 0x64, 0x1b, 0xb2, 0x9d, 0xa5, 0x6c, 0x2b, 0x4a,
            0x7f, 0x45, 0x69, 0x91, 0xfc, 0x92, 0xb2, 0x01, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
            0xff, 0x4d, 0x04, 0xff, 0xff, 0x00, 0x1d, 0x01, 0x04, 0x45, 0x54, 0x68,
            0x65, 0x20, 0x54, 0x69, 0x6d, 0x65, 0x73, 0x20, 0x30, 0x33, 0x2f, 0x4a,
            0x61, 0x6e, 0x2f, 0x32, 0x30, 0x30, 0x39, 0x20, 0x43, 0x68, 0x61, 0x6e,
            0x63, 0x65, 0x6c, 0x6c, 0x6f, 0x72, 0x20, 0x6f, 0x6e, 0x20, 0x62, 0x72,
            0x69, 0x6e, 0x6b, 0x20, 0x6f, 0x66, 0x20, 0x73, 0x65, 0x63, 0x6f, 0x6e,
            0x64, 0x20, 0x62, 0x61, 0x69, 0x6c, 0x6f, 0x75, 0x74, 0x20, 0x66, 0x6f,
            0x72, 0x20, 0x62, 0x61, 0x6e, 0x6b, 0x73, 0xff, 0xff, 0xff, 0xff, 0x01,
            0x00, 0xf2, 0x05, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x43, 0x41, 0x04, 0x67,
            0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48, 0x27, 0x19, 0x67, 0xf1, 0xa6, 0x71,
            0x30, 0xb7, 0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09, 0xa6, 0x79,
            0x62, 0xe0, 0xea, 0x1f, 0x61, 0xde, 0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c,
            0xef, 0x38, 0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12, 0xde, 0x5c,
            0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d, 0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b,
            0xf1, 0x1d, 0x5f, 0xac, 0x00, 0x00, 0x00, 0x00
        };

        // make blk.dat consists from 2 genesis blocks
        for (uint32_t i = 0; i < 2; ++i)
            for (uint32_t j = 0; j < sizeof(blockOnDisk); ++j) {
                fwrite(&blockOnDisk[j], 1, 1, fileIn);
            }
        rewind(fileIn);

        CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE(10000000), MAX_BLOCK_SIZE(10000000)+8, SER_DISK, CLIENT_VERSION);
        blkdat.SetLimit(); // remove former limit

        blkdat.SetPos(0);
        for (uint32_t i = 0; i < 2; ++i) {
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat >> FLATDATA(buf);
                ASSERT_TRUE(*(uint32_t *)buf == 0x8DE4EEF9);
                // read size
                blkdat >> nSize;
                ASSERT_FALSE(nSize < 80 || nSize > MAX_BLOCK_SIZE(10000000));
            } catch (const std::exception&) {
                ASSERT_TRUE(false);
            }
            // Read block
            blkdat >> block;
            ASSERT_TRUE(block.GetHash().ToString() == "027e3758c3a65b12aa1046462b486d0a63bfa1beae327897f56c5cfb7daaae71");
        }

        block.SetNull();
        blkdat.SetPos(8);
        blkdat >> block;
        ASSERT_TRUE(block.GetHash().ToString() == "027e3758c3a65b12aa1046462b486d0a63bfa1beae327897f56c5cfb7daaae71");

        blkdat.fclose();
        boost::filesystem::remove("squishy_block_load_tmp");

    }

}