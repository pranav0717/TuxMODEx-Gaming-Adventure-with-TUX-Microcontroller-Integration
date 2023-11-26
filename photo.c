/*									tab:8
 *
 * photo.c - photo display functions
 *
 * "Copyright (c) 2011 by Steven S. Lumetta."
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE AUTHOR OR THE UNIVERSITY OF ILLINOIS BE LIABLE TO 
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL 
 * DAMAGES ARISING OUT  OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, 
 * EVEN IF THE AUTHOR AND/OR THE UNIVERSITY OF ILLINOIS HAS BEEN ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHOR AND THE UNIVERSITY OF ILLINOIS SPECIFICALLY DISCLAIM ANY 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE 
 * PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND NEITHER THE AUTHOR NOR
 * THE UNIVERSITY OF ILLINOIS HAS ANY OBLIGATION TO PROVIDE MAINTENANCE, 
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    3
 * Creation Date:   Fri Sep  9 21:44:10 2011
 * Filename:	    photo.c
 * History:
 *	SL	1	Fri Sep  9 21:44:10 2011
 *		First written (based on mazegame code).
 *	SL	2	Sun Sep 11 14:57:59 2011
 *		Completed initial implementation of functions.
 *	SL	3	Wed Sep 14 21:49:44 2011
 *		Cleaned up code for distribution.
 */


#include <string.h>

#include "assert.h"
#include "modex.h"
#include "photo.h"
#include "photo_headers.h"
#include "world.h"



/* types local to this file (declared in types.h) */

/* 
 * A room photo.  Note that you must write the code that selects the
 * optimized palette colors and fills in the pixel data using them as 
 * well as the code that sets up the VGA to make use of these colors.
 * Pixel data are stored as one-byte values starting from the upper
 * left and traversing the top row before returning to the left of
 * the second row, and so forth.  No padding should be used.
 */
struct photo_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t        palette[192][3];     /* optimized palette colors */
    uint8_t*       img;                 /* pixel data               */
};

/* 
 * An object image.  The code for managing these images has been given
 * to you.  The data are simply loaded from a file, where they have 
 * been stored as 2:2:2-bit RGB values (one byte each), including 
 * transparent pixels (value OBJ_CLR_TRANSP).  As with the room photos, 
 * pixel data are stored as one-byte values starting from the upper 
 * left and traversing the top row before returning to the left of the 
 * second row, and so forth.  No padding is used.
 */
struct image_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t*       img;                 /* pixel data               */
};

////////////////////////////////////////////////////////////////* MY OCTREE NEEDS*/////////////////////////////////////////////////////////////////
/*Interface Description:
The Octree structure represents a tree data structure used for color quantization or similar purposes.
It consists of two levels: Level2 and Level4, with Level2 containing 64 nodes and Level4 containing 4096 nodes.
Each node in the Octree_Node structure stores color information (RGB), sums of color components (Sum_R, Sum_G, Sum_B), 
a count of the number of colors represented by the node (count)*/

// Define the structure for a node in the Octree
typedef struct Octree_Node {
    uint32_t RGB;
    uint32_t Sum_R;
    uint32_t Sum_G;
    uint32_t Sum_B;
    uint32_t count;
    uint32_t sorted_index; // A field to track sorted indices (bug log never kept track)
} Octree_Node;

// Define the Octree structure
typedef struct Octree {
    Octree_Node Level2[64];    // Array of nodes at level 2 (8^2 = 64 nodes)
    Octree_Node Level4[4096];  // Array of nodes at level 4 (8^4 = 4096 nodes)
} Octree;
////////////////////////////////////////////////////////////////* MY OCTREE NEEDS*/////////////////////////////////////////////////////////////////

/* file-scope variables */

/* 
 * The room currently shown on the screen.  This value is not known to 
 * the mode X code, but is needed when filling buffers in callbacks from 
 * that code (fill_horiz_buffer/fill_vert_buffer).  The value is set 
 * by calling prep_room.
 */
static const room_t* cur_room = NULL; 

/* 
 * fill_horiz_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the leftmost 
 *                pixel of a line to be drawn on the screen, this routine 
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- leftmost pixel of line to be drawn 
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_horiz_buffer (int x, int y, unsigned char buf[SCROLL_X_DIM])
{
    int            idx;   /* loop index over pixels in the line          */ 
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgx;  /* loop index over pixels in object image      */ 
    int            yoff;  /* y offset into object image                  */ 
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_X_DIM; idx++) {
        buf[idx] = (0 <= x + idx && view->hdr.width > x + idx ?
		    view->img[view->hdr.width * y + x + idx] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (y < obj_y || y >= obj_y + img->hdr.height ||
	    x + SCROLL_X_DIM <= obj_x || x >= obj_x + img->hdr.width) {
	    continue;
	}

	/* The y offset of drawing is fixed. */
	yoff = (y - obj_y) * img->hdr.width;

	/* 
	 * The x offsets depend on whether the object starts to the left
	 * or to the right of the starting point for the line being drawn.
	 */
	if (x <= obj_x) {
	    idx = obj_x - x;
	    imgx = 0;
	} else {
	    idx = 0;
	    imgx = x - obj_x;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_X_DIM > idx && img->hdr.width > imgx; idx++, imgx++) {
	    pixel = img->img[yoff + imgx];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/* 
 * fill_vert_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the top pixel of 
 *                a vertical line to be drawn on the screen, this routine 
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- top pixel of line to be drawn 
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_vert_buffer (int x, int y, unsigned char buf[SCROLL_Y_DIM])
{
    int            idx;   /* loop index over pixels in the line          */ 
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgy;  /* loop index over pixels in object image      */ 
    int            xoff;  /* x offset into object image                  */ 
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_Y_DIM; idx++) {
        buf[idx] = (0 <= y + idx && view->hdr.height > y + idx ?
		    view->img[view->hdr.width * (y + idx) + x] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (x < obj_x || x >= obj_x + img->hdr.width ||
	    y + SCROLL_Y_DIM <= obj_y || y >= obj_y + img->hdr.height) {
	    continue;
	}

	/* The x offset of drawing is fixed. */
	xoff = x - obj_x;

	/* 
	 * The y offsets depend on whether the object starts below or 
	 * above the starting point for the line being drawn.
	 */
	if (y <= obj_y) {
	    idx = obj_y - y;
	    imgy = 0;
	} else {
	    idx = 0;
	    imgy = y - obj_y;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_Y_DIM > idx && img->hdr.height > imgy; idx++, imgy++) {
	    pixel = img->img[xoff + img->hdr.width * imgy];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/* 
 * image_height
 *   DESCRIPTION: Get height of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
image_height (const image_t* im)
{
    return im->hdr.height;
}


/* 
 * image_width
 *   DESCRIPTION: Get width of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
image_width (const image_t* im)
{
    return im->hdr.width;
}

/* 
 * photo_height
 *   DESCRIPTION: Get height of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
photo_height (const photo_t* p)
{
    return p->hdr.height;
}


/* 
 * photo_width
 *   DESCRIPTION: Get width of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
photo_width (const photo_t* p)
{
    return p->hdr.width;
}


/*  changes 
 * prep_room
 *   DESCRIPTION: Prepare a new room for display.  You might want to set
 *                up the VGA palette registers according to the color
 *                palette that you chose for this room.
 *   INPUTS: r -- pointer to the new room
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: changes recorded cur_room for this file
 */
void
prep_room (const room_t* r)
{
	set_palette(room_photo(r)->palette); //last line of doc?? Finf this func: shoudl be a palette_RGB[192][3]?? prolly have to make look modex
    /* Record the current room. */
    cur_room = r;
}


/* 
 * read_obj_image
 *   DESCRIPTION: Read size and pixel data in 2:2:2 RGB format from a
 *                photo file and create an image structure from it.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the image
 */
image_t*
read_obj_image (const char* fname)
{
    FILE*    in;		/* input file               */
    image_t* img = NULL;	/* image structure          */
    uint16_t x;			/* index over image columns */
    uint16_t y;			/* index over image rows    */
    uint8_t  pixel;		/* one pixel from the file  */

    /* 
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the image pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (img = malloc (sizeof (*img))) ||
	NULL != (img->img = NULL) || /* false clause for initialization */
	1 != fread (&img->hdr, sizeof (img->hdr), 1, in) ||
	MAX_OBJECT_WIDTH < img->hdr.width ||
	MAX_OBJECT_HEIGHT < img->hdr.height ||
	NULL == (img->img = malloc 
		 (img->hdr.width * img->hdr.height * sizeof (img->img[0])))) {
	if (NULL != img) {
	    if (NULL != img->img) {
	        free (img->img);
	    }
	    free (img);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }

    /* 
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = img->hdr.height; y-- > 0; ) {

	/* Loop over columns from left to right. */
	for (x = 0; img->hdr.width > x; x++) {

	    /* 
	     * Try to read one 8-bit pixel.  On failure, clean up and 
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (img->img);
		free (img);
	        (void)fclose (in);
		return NULL;
	    }

	    /* Store the pixel in the image data. */
	    img->img[img->hdr.width * y + x] = pixel;
	}
    }

    /* All done.  Return success. */
    (void)fclose (in);
    return img;
}


/* 
 * read_photo
 *   DESCRIPTION: Read size and pixel data in 5:6:5 RGB format from a
 *                photo file and create a photo structure from it.
 *                Code provided simply maps to 2:2:2 RGB.  You must
 *                replace this code with palette color selection, and
 *                must map the image pixels into the palette colors that
 *                you have defined.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the photo
 */
photo_t*
read_photo (const char* fname)
{
    FILE*    in;	/* input file               */
    photo_t* p = NULL;	/* photo structure          */
    uint16_t x;		/* index over image columns */
    uint16_t y;		/* index over image rows    */
    uint16_t pixel;	/* one pixel from the file  */

    /* 
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the photo pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (p = malloc (sizeof (*p))) ||
	NULL != (p->img = NULL) || /* false clause for initialization */
	1 != fread (&p->hdr, sizeof (p->hdr), 1, in) ||
	MAX_PHOTO_WIDTH < p->hdr.width ||
	MAX_PHOTO_HEIGHT < p->hdr.height ||
	NULL == (p->img = malloc 
		 (p->hdr.width * p->hdr.height * sizeof (p->img[0])))) {
	if (NULL != p) {
	    if (NULL != p->img) {
	        free (p->img);
	    }
	    free (p);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }

//////////////////////////////////////////////////////////////////////////me//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////me boo////////////////////////////////////////////////////////////////////////
		Octree Tree = create_Octree(); //initialize the octree i made
		uint32_t color_12_bit;
		uint32_t color_6_bit;
		uint32_t Red,Green,Blue;
		uint32_t numPixels = p->hdr.width * p->hdr.height;
		uint32_t array_of_image[numPixels]; // array with all pixels making image!
		uint32_t Level4_idx; //index on level 4 will need this at sorting stage!
////////////////////////////////////////////////////////////////////////me boo////////////////////////////////////////////////////////////////////////
	/*  
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = p->hdr.height; y-- > 0; ) {
	/* Loop over columns from left to right. */
		for (x = 0; p->hdr.width > x; x++) {

	    /* 
	     * Try to read one 16-bit pixel.  On failure, clean up and 
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (p->img);
		free (p);
	        (void)fclose (in);
		return NULL;

	    }

		/*
		The first step in the algorithm requires that you count the number of pixels in each node at level four of an octree. Define a structure for 
		all relevant data for a level-four octree node, then create an array of that structure for your algorithm. Go through the pixels in the image
		file, map them into the appropriate octree node, and count them up.
		*/
		array_of_image[p->hdr.width * y + x] = pixel; //exact position: offset from currnt row + what column ie. position in array to store pixel

		color_12_bit = convert16_to_12(pixel); // my helper function

		// Extract Red, Green, and Blue components
        Red = extractRed(pixel); //helper
        Green = extractGreen(pixel); //helper 
        Blue = extractBlue(pixel); //helper

        // Updating the Octree data structure i made
		Tree.Level4[color_12_bit].count +=1;
		Tree.Level4[color_12_bit].Sum_R += Red;
		Tree.Level4[color_12_bit].Sum_G += Green;
		Tree.Level4[color_12_bit].Sum_B += Blue;

	    /* take this out as a whole and deal with level 4 of octree???? -----pranav
	     * 16-bit pixel is coded as 5:6:5 RGB (5 bits red, 6 bits green,
	     * and 6 bits blue).  We change to 2:2:2, which we've set for the
	     * game objects.  You need to use the other 192 palette colors
	     * to specialize the appearance of each photo.
	     *
	     * In this code, you need to calculate the p->palette values,
	     * which encode 6-bit RGB as arrays of three uint8_t's.  When
	     * the game puts up a photo, you should then change the palette 
	     * to match the colors needed for that photo.
	     */
// 	    p->img[p->hdr.width * y + x] = (((pixel >> 14) << 4) |
// 					    (((pixel >> 9) & 0x3) << 2) |
// 					    ((pixel >> 3) & 0x3));
	}
}

	int i;
	int L2_index;
	// Sort the Level4 array
	qsort((Octree_Node*)Tree.Level4, 4096, sizeof(Tree.Level4[0]), compareFunction);

	// Set up the palette
	for (i = 0; i < 4096; i++) {
		// Record the sorted index
		int currentRGB = Tree.Level4[i].RGB;
		Tree.Level4[currentRGB].sorted_index = i;
		
		if (i < 128) {
			// Set up the first 128 colors of the palette (L4)
			if (Tree.Level4[i].count == 0) {
				break;
			}
		double count_inverse = 1.0 / Tree.Level4[i].count; //got some crazy divide bby zero error. bug log
		p->palette[i][0] = Tree.Level4[i].Sum_R * count_inverse;
		p->palette[i][1] = Tree.Level4[i].Sum_G * count_inverse;
		p->palette[i][2] = Tree.Level4[i].Sum_B * count_inverse;
		}
	}

	// Use the remaining L4 colors to set up L2
	for (i = 128; i < 4096; i++) {
		if (Tree.Level4[i].count == 0) {
			continue;
		}
		
		// Calculate L2_index once
		L2_index = convert12_to_6(Tree.Level4[i].RGB);
		
		Tree.Level2[L2_index].Sum_R += Tree.Level4[i].Sum_R;
		Tree.Level2[L2_index].Sum_G += Tree.Level4[i].Sum_G;
		Tree.Level2[L2_index].Sum_B += Tree.Level4[i].Sum_B;
		Tree.Level2[L2_index].count += Tree.Level4[i].count;
	}

	// Set up the last 64 colors of the palette (L2)
	for (i = 0; i < 64; i++) {
		if (Tree.Level2[i].count == 0) {
			continue;
		}
		
		int paletteIndex = i + 128;
		double count_inverse = 1.0 / Tree.Level2[i].count;
		p->palette[paletteIndex][0] = Tree.Level2[i].Sum_R * count_inverse;
		p->palette[paletteIndex][1] = Tree.Level2[i].Sum_G * count_inverse;
		p->palette[paletteIndex][2] = Tree.Level2[i].Sum_B * count_inverse;
	}

	// Loop over each pixel in the image
	for (y = p->hdr.height; y-- > 0;) {
		for (x = 0; p->hdr.width > x; x++) {
			// Get the pixel from the array_of_image
			pixel = array_of_image[p->hdr.width * y + x];

			// Convert the pixel into 12 and 6-bit color types
			color_12_bit = convert16_to_12(pixel);
			color_6_bit = convert12_to_6(color_12_bit);

			// Find the sorted index of the color in Level4
			Level4_idx = Tree.Level4[color_12_bit].sorted_index;

			// Assign it to the appropriate range
			if (Level4_idx < 128) {
				// Assign to range [64, 191]
				p->img[p->hdr.width * y + x] = (char)(64 + Level4_idx);
			} else {
				// Assign to range [192, 255]
				p->img[p->hdr.width * y + x] = (char)(64 + 128 + color_6_bit);
			}
		}
	}

	//     /* All done.  Return success. */
		(void)fclose (in);
		return p;
	}

//////////////////////////////////////////////////////////////////////////me//////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////* MY OCTREE NEEDS*/////////////////////////////////////////////////////////////////

/* 
   Interface Description:
   This function creates and initializes an Octree structure for color quantization purposes.
   The Octree consists of two levels: Level2 and Level4, with Level2 containing 64 nodes and Level4 containing 4096 nodes.
   Each node in the Octree_Node structure stores color information (RGB), sums of color components (Sum_R, Sum_G, Sum_B),
   a count of the number of colors represented by the node (count).

   Returns:
   An initialized Octree structure.
*/

// Function to create and initialize an octree node
Octree create_Octree() {
    // Allocate memory for the Octree structure
    Octree Tree;
    int i;

    // Initialize both level 2 nodes
    for (i = 0; i < 64; i++) {
        Tree.Level2[i].count = 0;
        Tree.Level2[i].Sum_R = 0;
        Tree.Level2[i].Sum_G = 0;
        Tree.Level2[i].Sum_B = 0;
        Tree.Level2[i].RGB = i;
    }

    // Initialize all level 4 nodes
    for (i = 0; i < 4096; i++) {
        Tree.Level4[i].count = 0;
        Tree.Level4[i].Sum_R = 0;
        Tree.Level4[i].Sum_G = 0;
        Tree.Level4[i].Sum_B = 0;
        Tree.Level4[i].RGB = i;
    }

    return Tree;
}


/*
16-bit pixel format:   0bRRRRRGGGGGGBBBBB
                       |   |      |     |
                       |   |      |     Extract the 4-bit Blue component
                       |   |      Extract the 4-bit Green component
                       |   Extract the 4-bit Red component
                       Left-shift Red by 4 bits
                       Left-shift Green by 4 bits
                       Right-shift Blue by 1 bit (to adjust for its position)

12-bit output format:  0bRRRR-GGGG-BBBB
*/

uint32_t convert16_to_12(uint16_t pixel) {
    // Extract the 4 most significant bits of each color component
    uint32_t Red = (pixel >> 12) & 0xF;  // Extract 4 bits for Red (RRRR)
    uint32_t Green = (pixel >> 7) & 0xF; // Extract 4 bits for Green (GGGG)
    uint32_t Blue = (pixel >> 1) & 0xF;  // Extract 4 bits for Blue (BBBB)

    // Combine and pack the components into a 12-bit color
    // Math explanation for combining:
    // Red is shifted 8 bits to the left (RRRR0000)
    // Green is shifted 4 bits to the left (0000GGGG)
    // Blue is as-is (0000BBBB)
    // Combining them using bitwise OR to get 12-bit format (RRRRGGGGBBBB)
    uint32_t result = ((Red << 8) | (Green << 4) | Blue);

    return result;
}

/*
Input 12-bit pixel:  0bRRRRGGGGBBBB
                      |  |     |    |
                      |  |     |    Extract the 4-bit Blue component
                      |  |     Extract the 4-bit Green component
                      |  Extract the 4-bit Red component
                      Left-shift Red by Red_6off bits
                      Left-shift Green by Green_6off bits

Output 6-bit value:   0bRRGGBB
*/

uint32_t convert12_to_6(uint16_t pixel) {
    // Extract the 2 most significant bits of each color component
    uint32_t Red = (pixel >> 10) & 0x3;  // Extract 2 bits for Red (RR)
    uint32_t Green = (pixel >> 6) & 0x3; // Extract 2 bits for Green (GG)
    uint32_t Blue = (pixel >> 2) & 0x3;  // Extract 2 bits for Blue (BB)

    // Combine and pack the components into a 6-bit color
    // Math explanation for combining:
    // Red is shifted 4 bits to the left (RR00)
    // Green is shifted 2 bits to the left (00GG)
    // Blue is as-is (BB)
    // Combining them using bitwise OR to get 6-bit format (RRGGBB)
    uint32_t result = ((Red << 4) | (Green << 2) | Blue);

    return result;
}

// Function to extract the Red component from a 16-bit RGB pixel
uint32_t extractRed(uint16_t pixel) {
    // Right-shift by the number of bits for Green and Blue (5 bits in total)
    // to extract the 5-bit Red component, and then left-shift by 1 bit.
    return ((pixel >> 11) & 0x1F) << 1;
}

// Function to extract the Green component from a 16-bit RGB pixel
uint32_t extractGreen(uint16_t pixel) {
    // Right-shift by the number of bits for Blue (5 bits) to extract the
    // 5-bit Green component.
    return ((pixel >> 5) & 0x3F);
}

// Function to extract the Blue component from a 16-bit RGB pixel
uint32_t extractBlue(uint16_t pixel) {
    // Mask the lowest 5 bits to extract the 5-bit Blue component.
    return (pixel & 0x1F) << 1;
}

/* 
   Interface Description:
   This function is used as a comparison function for sorting Octree_Node structures
   in descending order based on their 'count' member. It is intended to be used with
   the standard library function 'qsort' to sort an array of Octree_Node structures.

   Parameters:
   - a: Pointer to the first Octree_Node structure to compare.
   - b: Pointer to the second Octree_Node structure to compare.

   Returns:
   - A positive value if 'b->count' is greater than 'a->count'.
   - A negative value if 'a->count' is greater than 'b->count'.
   - 0 if 'a->count' is equal to 'b->count'.
*/

int compareFunction(const void *a, const void *b) {
    // Cast 'a' and 'b' to Octree_Node pointers and compare their 'count' members
    return (((Octree_Node*)b)->count - ((Octree_Node*)a)->count);
}
////////////////////////////////////////////////////////////////* MY OCTREE NEEDS*/////////////////////////////////////////////////////////////////
