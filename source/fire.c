#include <stdbool.h>
#include <string.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <ogc/video_types.h>
#include <ogc/gx_struct.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>

#define WIDTH 80
#define HEIGHT 50

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

static void rgb_to_yuyv422_and_scale(void *dst, const void *src, int orig_width,
                                     int orig_height, int new_width, int new_height) {
	const unsigned char *srcp = src;
	unsigned char *dstp = dst;

	float x_scale = (float)orig_width / (float)new_width;
	float y_scale = (float)orig_height / (float)new_height;

	for (int y = 0; y < new_height; y++) {
		for (int x = 0; x < new_width; x += 2) {
			int in_x1 = (int)(x * x_scale);
			int in_y = (int)(y * y_scale);
			int in_x2 = in_x1 + 1;

			int out_idx = y * new_width * 2 + x * 2;

			int r1 = srcp[(in_y * orig_width + in_x1) * 4 + 1];
			int g1 = srcp[(in_y * orig_width + in_x1) * 4 + 2];
			int b1 = srcp[(in_y * orig_width + in_x1) * 4 + 3];

			int r2 = srcp[(in_y * orig_width + in_x2) * 4 + 1];
			int g2 = srcp[(in_y * orig_width + in_x2) * 4 + 2];
			int b2 = srcp[(in_y * orig_width + in_x2) * 4 + 3];

			unsigned char y1 = (unsigned char)(0.257 * r1 + 0.504 * g1 + 0.098 * b1 + 16);
			unsigned char u = (unsigned char)(-0.148 * r1 - 0.291 * g1 + 0.439 * b1 + 128);
			unsigned char y2 = (unsigned char)(0.257 * r2 + 0.504 * g2 + 0.098 * b2 + 16);
			unsigned char v = (unsigned char)(0.439 * r1 - 0.368 * g1 - 0.071 * b1 + 128);

			dstp[out_idx] = y1;
			dstp[out_idx + 1] = u;
			dstp[out_idx + 2] = y2;
			dstp[out_idx + 3] = v;
		}
	}
}

static const uint32_t palette[256] = {
/* Jare's original FirePal. */
#define C(r,g,b) ((((r) * 4) << 16) | ((g) * 4 << 8) | ((b) * 4))
C( 0,   0,   0), C( 0,   1,   1), C( 0,   4,   5), C( 0,   7,   9),
C( 0,   8,  11), C( 0,   9,  12), C(15,   6,   8), C(25,   4,   4),
C(33,   3,   3), C(40,   2,   2), C(48,   2,   2), C(55,   1,   1),
C(63,   0,   0), C(63,   0,   0), C(63,   3,   0), C(63,   7,   0),
C(63,  10,   0), C(63,  13,   0), C(63,  16,   0), C(63,  20,   0),
C(63,  23,   0), C(63,  26,   0), C(63,  29,   0), C(63,  33,   0),
C(63,  36,   0), C(63,  39,   0), C(63,  39,   0), C(63,  40,   0),
C(63,  40,   0), C(63,  41,   0), C(63,  42,   0), C(63,  42,   0),
C(63,  43,   0), C(63,  44,   0), C(63,  44,   0), C(63,  45,   0),
C(63,  45,   0), C(63,  46,   0), C(63,  47,   0), C(63,  47,   0),
C(63,  48,   0), C(63,  49,   0), C(63,  49,   0), C(63,  50,   0),
C(63,  51,   0), C(63,  51,   0), C(63,  52,   0), C(63,  53,   0),
C(63,  53,   0), C(63,  54,   0), C(63,  55,   0), C(63,  55,   0),
C(63,  56,   0), C(63,  57,   0), C(63,  57,   0), C(63,  58,   0),
C(63,  58,   0), C(63,  59,   0), C(63,  60,   0), C(63,  60,   0),
C(63,  61,   0), C(63,  62,   0), C(63,  62,   0), C(63,  63,   0),
/* Followed by "white heat". */
#define W C(63,63,63)
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W,
W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W, W
#undef W
#undef C
};

static uint8_t fire[WIDTH * HEIGHT];
static uint8_t prev_fire[WIDTH * HEIGHT];
static uint32_t framebuf[WIDTH * HEIGHT];
static uint8_t yuyv[SCREEN_WIDTH * SCREEN_HEIGHT * 2];

int main(void) {
	int i;
	uint32_t sum;
	uint8_t avg;

	VIDEO_Init();

	PAD_Init();
	WPAD_Init();

	GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
	void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	while (true) {
		// Wait until the end of the frame
		VIDEO_WaitVSync();

		PAD_ScanPads();
		for (unsigned char i = 0; i < 4; ++i)
			if (PAD_ButtonsDown(i) & PAD_BUTTON_START)
				return 0;

		WPAD_ScanPads();
		for (unsigned char i = 0; i < 4; ++i)
			if (WPAD_ButtonsDown(i) & WPAD_BUTTON_HOME)
				return 0;

		for (i = WIDTH + 1; i < (HEIGHT - 1) * WIDTH - 1; i++) {
			/* Average the eight neighbours. */
			sum = prev_fire[i - WIDTH - 1] +
			      prev_fire[i - WIDTH    ] +
			      prev_fire[i - WIDTH + 1] +
			      prev_fire[i - 1] +
			      prev_fire[i + 1] +
			      prev_fire[i + WIDTH - 1] +
			      prev_fire[i + WIDTH    ] +
			      prev_fire[i + WIDTH + 1];
			avg = (uint8_t)(sum / 8);

			/* "Cool" the pixel if the two bottom bits of the
			   sum are clear (somewhat random). For the bottom
			   rows, cooling can overflow, causing "sparks". */
			if (!(sum & 3) &&
			    (avg > 0 || i >= (HEIGHT - 4) * WIDTH)) {
				avg--;
			}
			fire[i] = avg;
		}

		/* Copy back and scroll up one row.
		   The bottom row is all zeros, so it can be skipped. */
		for (i = 0; i < (HEIGHT - 2) * WIDTH; i++) {
			prev_fire[i] = fire[i + WIDTH];
		}

		/* Remove dark pixels from the bottom rows (except again the
		   bottom row which is all zeros). */
		for (i = (HEIGHT - 7) * WIDTH; i < (HEIGHT - 1) * WIDTH; i++) {
			if (fire[i] < 15) {
				fire[i] = 22 - fire[i];
			}
		}

		/* Copy to framebuffer and map to RGBA, scrolling up one row. */
		for (i = 0; i < (HEIGHT - 2) * WIDTH; i++) {
			framebuf[i] = palette[fire[i + WIDTH]];
		}

		// Convert from RGB to YUYV422 while scaling from 80x50 to 640x480
		rgb_to_yuyv422_and_scale(yuyv, framebuf, WIDTH, HEIGHT - 2, SCREEN_WIDTH, SCREEN_HEIGHT);

		// Copy to the Wii's framebuffer
		for (i = 0; i < SCREEN_HEIGHT; i++)
			memcpy(xfb + (i * rmode->fbWidth * 2), yuyv + (i * (SCREEN_WIDTH * 2)), SCREEN_WIDTH * 2);
	}

	return 0;
}
