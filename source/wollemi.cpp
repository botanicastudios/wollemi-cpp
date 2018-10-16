/* -*- mode: c; c-file-style: "openbsd" -*- */
/* TODO:5002 You may want to change the copyright of all files. This is the
 * TODO:5002 ISC license. Choose another one if you want.
 */
/*
 * Copyright (c) 2018 Toby Marsden <toby@botanicastudios.io>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wollemi.h"

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <math.h>

#include <png.h>

#include <sys/resource.h>
#include <sys/time.h>

#include "cJSON.h"

#include "epd7in5.h"
#include "epdif.h"

using namespace std;

static const char* PACKAGE_VERSION = "0.1";
static const char* PACKAGE         = "Wollemi";

static const unsigned int DISPLAY_WIDTH  = 384;
static const unsigned int DISPLAY_HEIGHT = 640;
static const char*        SOCKET_PATH    = "/tmp/wollemi";

inline double get_time()
{
    struct timeval  t;
    struct timezone tzp;
    gettimeofday(&t, &tzp);
    return t.tv_sec + t.tv_usec * 1e-6 * 1000;
}

extern const char* __progname;

static void usage(void)
{
    /* TODO:3002 Don't forget to update the usage block with the most
     * TODO:3002 important options. */
    fprintf(stderr, "Usage: %s [OPTIONS]\n", __progname);
    fprintf(stderr, "Version: %s\n", PACKAGE_VERSION);
    fprintf(stderr, "\n");
    fprintf(stderr, " -d, --debug        be more verbose.\n");
    fprintf(stderr, " -h, --help         display help and exit\n");
    fprintf(stderr, " -v, --version      print version and exit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "see manual page %s (8) for more information\n", PACKAGE);
}

static unsigned int width, height;
static png_byte     color_type;
static png_byte     bit_depth;
static unsigned int bytes_per_pixel;
static png_bytep*   row_pointers;

static void read_png_file(char* filename)
{
    FILE* fp = fopen(filename, "rb");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);

    png_read_info(png, info);

    width      = png_get_image_width(png, info);
    height     = png_get_image_height(png, info);
    color_type = png_get_color_type(png, info);
    bit_depth  = png_get_bit_depth(png, info);

    // Read any color_type into 8bit depth, RGBA format.
    // See http://www.libpng.org/pub/png/libpng-manual.txt

    if (bit_depth == 16) png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);

    // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);

    // These color_type don't have an alpha channel then fill it with 0xff.
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    bytes_per_pixel = static_cast<unsigned int>(png_get_rowbytes(png, info) / width);

    row_pointers = static_cast<png_bytep*>(malloc(sizeof(png_bytep) * height));
    for (uint y = 0; y < height; y++)
    {
        row_pointers[y] = static_cast<png_byte*>(malloc(png_get_rowbytes(png, info)));
    }

    png_read_image(png, row_pointers);

    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
    png  = NULL;
    info = NULL;
}

/*
 * Lift the gamma curve from the pixel from the source image so we can convert
 * to grayscale
 */
inline double sRGB_to_linear(double x)
{
    if (x < 0.04045) return x / 12.92;
    return pow((x + 0.055) / 1.055, 2.4);
}

/*
 * Apply gamma curve to the pixel of the destination image again
 */
inline double linear_to_sRGB(double y)
{
    if (y <= 0.0031308) return 12.92 * y;
    return 1.055 * pow(y, 1 / 2.4) - 0.055;
}

int main(int argc, char* argv[])
{
    int debug = 1;
    int ch;

    /* TODO:3001 If you want to add more options, add them here. */
    static struct option long_options[] = {{"debug", no_argument, 0, 'd'},
                                           {"help", no_argument, 0, 'h'},
                                           {"version", no_argument, 0, 'v'}};
    while (1)
    {
        int option_index = 0;
        ch               = getopt_long(argc, argv, "hvdD:", long_options, &option_index);
        if (ch == -1) break;
        switch (ch)
        {
        case 'h': usage(); exit(0);
        case 'v': fprintf(stdout, "%s\n", PACKAGE_VERSION); exit(0);
        case 'd': debug++; break;
        case 'D':
            // TODO re-enable
            // log_accept(optarg);
            break;
        default:
            fprintf(stderr, "unknown option `%c'\n", ch);
            usage();
            exit(1);
        }
    }

    // TODO re-enable
    // log_init(debug, __progname);

    /*
     * We will receive a JSON encoded message on the UNIX socket, in this format:
     * {"type":"message","data":{"action":"refresh","image":"/path/to/the/image.png"}}
     */

    struct sockaddr_un addr;
    char               buf[100];
    int                fd, cl, rc;

    if (argc > 1) SOCKET_PATH = argv[1];

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*SOCKET_PATH == '\0')
    {
        *addr.sun_path = '\0';
        strncpy(addr.sun_path + 1, SOCKET_PATH + 1, sizeof(addr.sun_path) - 2);
    }
    else
    {
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        unlink(SOCKET_PATH);
    }

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1)
    {
        perror("bind error");
        exit(-1);
    }

    if (listen(fd, 5) == -1)
    {
        perror("listen error");
        exit(-1);
    }

    while (1)
    {
        if ((cl = accept(fd, NULL, NULL)) == -1)
        {
            perror("accept error");
            continue;
        }

        while ((rc = static_cast<int>(read(cl, buf, sizeof(buf)))) > 0)
        {
            printf("read...\n");
            printf("%.*s\n", rc, buf);
            double time_start     = get_time();
            cJSON* message_json   = cJSON_Parse(buf);
            cJSON* data           = NULL;
            cJSON* action         = NULL;
            cJSON* image_filename = NULL;
            data                  = cJSON_GetObjectItemCaseSensitive(message_json, "data");
            if (cJSON_IsObject(data))
            {
                action         = cJSON_GetObjectItemCaseSensitive(data, "action");
                image_filename = cJSON_GetObjectItemCaseSensitive(data, "image");
                if (cJSON_IsString(action) && (strcmp(action->valuestring, "refresh") == 0) &&
                    cJSON_IsString(image_filename) && (image_filename->valuestring != NULL))
                {
                    printf("Displaying image file at \"%s\"\n", image_filename->valuestring);

                    // Populate the row pointers with pixel data from the PNG image,
                    // in RGB(A) format, using libpng
                    read_png_file(image_filename->valuestring);

                    /*
                     * The bitmap frame buffer will consist of bytes (i.e. char)
                     * in a vector. For a 1-bit display, each byte represents 8
                     * 1-bit pixels. It will therefore be 1/8 of the width of the
                     * display, times its full height.
                     */
                    unsigned int frame_buffer_length =
                        static_cast<unsigned int>(ceil(DISPLAY_WIDTH / 8) * DISPLAY_HEIGHT);
                    std::vector<unsigned char> bitmap_frame_buffer(frame_buffer_length);

                    /*
                     * Our `rows` will contain a 2D, 1-bit representation of the
                     * image, one vector element per pixel.
                     */
                    std::vector<std::vector<int>> rows(height, std::vector<int>(width));

                    for (unsigned int y = 0; y < height; y++)
                    {
                        for (unsigned int x = 0; x < width; x++)
                        {

                            // The row pointers contain RGB(A) data, RGB being
                            // stored in the first three bytes
                            uint R = row_pointers[y][x * bytes_per_pixel + 0];
                            uint G = row_pointers[y][x * bytes_per_pixel + 1];
                            uint B = row_pointers[y][x * bytes_per_pixel + 2];
                            uint gray_color;

                            // Convert the color to grayscale using a luminosity
                            // formula, which better represents human perception
                            double R_linear = sRGB_to_linear(R / 255.0);
                            double G_linear = sRGB_to_linear(G / 255.0);
                            double B_linear = sRGB_to_linear(B / 255.0);
                            double gray_linear =
                                0.2126 * R_linear + 0.7152 * G_linear + 0.0722 * B_linear;
                            gray_color =
                                static_cast<uint>(round(linear_to_sRGB(gray_linear) * 255));

                            // If a pixel is more than 50% bright, make it white. Otherwise, black.
                            rows[y][x] = (gray_color <= 127);
                        }
                    }

                    unsigned int bytes_per_row = DISPLAY_WIDTH / 8;

                    /*
                     * Convert the 2D matrix of 1-bit values into a flat array of
                     * 8-bit `char`s. To glob 8 bits together into a char, we use
                     * bit-shifting operators (<<)
                     */

                    // for each row of the display (not the image!)...
                    for (uint y = 0; y < DISPLAY_HEIGHT; y++)
                    {
                        // ... and each byte across (i.e. 1/8 of the columns in the display) ...
                        for (uint x = 0; x < bytes_per_row; x++)
                        {
                            unsigned int current_byte = 0;

                            // ... iterate over the byte's 8 bits representing the pixels in the
                            // image
                            for (uint xb = 0; xb < 8; xb++)
                            {
                                // if the image exists to fill the current bit, and the current
                                // bit/pixel is 1/black, assign the current bit to its position
                                // in a new byte based on its index and assign the current byte
                                // to a bitwise OR of this new byte.
                                // e.g. rows[1] =
                                // 1 1 0 1 0 0 1 1
                                // ^              current_byte = 00000000; xb = 0;
                                //                current_byte = current_byte | 1 << (7-xb)
                                //                current_byte = current_byte | 10000000
                                //                current_byte = 10000000;

                                //   ^            xb = 1;
                                //                current_byte = current_byte | 1 << (7-xb)
                                //                current_byte = current_byte | 01000000
                                //                current_byte = 11000000;

                                if ((width > (x + xb)) && (height > y) && rows[y][x + xb] == 1)
                                {
                                    current_byte = current_byte | 1 << (7 - xb);
                                    //    reinterpret_cast<unsigned char*>(1 << (7 - xb)));
                                }
                            }
                            // push each completed byte into the frame buffer
                            bitmap_frame_buffer[(y * bytes_per_row) + x] =
                                static_cast<unsigned char>(current_byte);
                            if (current_byte > 0)
                            {
                                printf("bitmap_frame_buffer[%d] = %02x\n", (y * bytes_per_row) + x,
                                       current_byte);
                            }
                        }
                    }

                    // unsigned int my_byte = 0;

                    // my_byte = my_byte | 0 << (7 - 0);
                    // my_byte = my_byte | 0 << (7 - 1);
                    // my_byte = my_byte | 1 << (7 - 2);
                    // my_byte = my_byte | 1 << (7 - 3);
                    // my_byte = my_byte | 0 << (7 - 4);
                    // my_byte = my_byte | 1 << (7 - 5);
                    // my_byte = my_byte | 1 << (7 - 6);
                    // my_byte = my_byte | 1 << (7 - 7);

                    // const unsigned char my_byte_char = reinterpret_cast<unsigned char>(&my_byte);
                    // char my_byte_char = static_cast<char>(my_byte);
                    // int* my_byte_int = reinterpret_cast<int*>(&my_byte_char);

                    // printf("\nmy_byte: %d\n", my_byte);
                    // printf("\nmy_byte: %02x\n", my_byte);
                    // printf("\nmy_byte_char: %c\n", my_byte_char);
                    // printf("\nmy_byte_char: %02x\n", my_byte_char);
                    // printf("\nmy_byte_int: %02x\n", my_byte_int);

                    // printf("bitmap_frame_buffer[0]: %c\n", bitmap_frame_buffer[0]);
                    printf("bitmap_frame_buffer.data(): %c\n", bitmap_frame_buffer.data());

                    Epd epd;
                    if (epd.Init() != 0) { printf("e-Paper init failed\n"); }
                    else
                    {
                        // send the frame buffer to the panel
                        epd.DisplayFrame(bitmap_frame_buffer.data());
                    }
                }
            }
            cJSON_Delete(message_json);
            double time_end = get_time();
            printf("Took %.2f ms\n", (time_end - time_start));
        }
        if (rc == -1)
        {
            perror("read");
            exit(-1);
        }
        else if (rc == 0)
        {
            printf("EOF\n");
            close(cl);
        }
    }

    return EXIT_SUCCESS;
}
