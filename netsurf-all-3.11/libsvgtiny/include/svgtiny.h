/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008 James Bursa <james@semichrome.net>
 */

#ifndef SVGTINY_H
#define SVGTINY_H

typedef int svgtiny_colour;
#define svgtiny_TRANSPARENT 0x1000000
#ifdef __riscos__
#define svgtiny_RGB(r, g, b) ((b) << 16 | (g) << 8 | (r))
#define svgtiny_RED(c) ((c) & 0xff)
#define svgtiny_GREEN(c) (((c) >> 8) & 0xff)
#define svgtiny_BLUE(c) (((c) >> 16) & 0xff)
#else
#define svgtiny_RGB(r, g, b) ((r) << 16 | (g) << 8 | (b))
#define svgtiny_RED(c) (((c) >> 16) & 0xff)
#define svgtiny_GREEN(c) (((c) >> 8) & 0xff)
#define svgtiny_BLUE(c) ((c) & 0xff)
#endif

typedef enum {
	svgtiny_FILL_NONZERO = 0,
	svgtiny_FILL_EVENODD = 1
} svgtiny_fill_rule;

typedef enum {
	svgtiny_FONT_SANS_SERIF = 0,
	svgtiny_FONT_SERIF = 1,
	svgtiny_FONT_MONOSPACE = 2
} svgtiny_font_family;

typedef enum {
	svgtiny_TEXT_ANCHOR_START = 0,
	svgtiny_TEXT_ANCHOR_MIDDLE = 1,
	svgtiny_TEXT_ANCHOR_END = 2
} svgtiny_text_anchor;

struct svgtiny_shape {
	float *path;
	unsigned int path_length;
	char *text;
	float text_x, text_y;
	float text_size;
	float text_rotation;
	svgtiny_font_family text_family;
	svgtiny_text_anchor text_anchor;
	int text_weight;
	int text_italic;
	svgtiny_colour fill;
	svgtiny_fill_rule fill_rule;
	svgtiny_colour stroke;
	int stroke_width;
};

struct svgtiny_diagram {
	int width, height;

	struct svgtiny_shape *shape;
	unsigned int shape_count;

	unsigned short error_line;
	const char *error_message;
};

typedef enum {
	svgtiny_OK,
	svgtiny_OUT_OF_MEMORY,
	svgtiny_LIBDOM_ERROR,
	svgtiny_NOT_SVG,
	svgtiny_SVG_ERROR
} svgtiny_code;

enum {
	svgtiny_PATH_MOVE,
	svgtiny_PATH_CLOSE,
	svgtiny_PATH_LINE,
	svgtiny_PATH_BEZIER
};

struct svgtiny_named_color {
	const char *name;
	svgtiny_colour color;
};


struct svgtiny_diagram *svgtiny_create(void);
svgtiny_code svgtiny_parse(struct svgtiny_diagram *diagram,
		const char *buffer, size_t size, const char *url,
		int width, int height);
void svgtiny_free(struct svgtiny_diagram *svg);

#endif
