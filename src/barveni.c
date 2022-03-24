#include <time.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <gtk/gtkbuilder.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pgm.h>
#include <ppm.h>
#include <math.h>
#include <cairo.h>
#include <gtkimageviewer-2.0/gtk-image-viewer.h>
#include <stdbool.h>

#define UNDOLEVELS 100 

/* Structure describing the position of the screen.  */
struct parameters {
  /* This is a center of rotation of screen (a green dot).  */
  double xstart, ystart;
  /* This defines horizontal vector of the screen.  */
  double xend, yend;
  /* I originally believed that Library of Congress scans are 1000 DPI
     and later found that they differs in their vertical and horisontal DPIs.
     XM is a correction to horisontal DPI and YM is a correction to vertical.
     Since the DPI information is inprecise this does not have any really 
     good meaning.  */
  double xm, ym;
  /* This was added later to allow distortions along each edge of the scan.
     It should be perpective correction in one direction and it is not.  */
  double xs,ys,xs2,ys2;
};

/* Undo history and the state of UI.  */
struct parameters undobuf[UNDOLEVELS];
struct parameters current;
int undopos;

char *oname, *paroname;
static void bigrender (int xoffset, int yoffset, double bigscale,
		       GdkPixbuf * bigpixbuf);

/* The graymap with original scan is stored here.  */
int xsize, ysize;
gray **graydata;
gray maxval;
int initialized = 0;

/* Status of the main window.  */
int offsetx = 8, offsety = 8;
int bigscale = 4;

bool display_scheduled = true;

/* How much is the image scaled in the small view.  */
#define SCALE 16


void
save_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  undobuf[undopos] = current;
}
void
undo_parameters (void)
{
  current = undobuf[undopos];
  undopos  = (undopos + UNDOLEVELS - 1) % UNDOLEVELS;
}
void
redo_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  current = undobuf[undopos];
}

double inline
cubicInterpolate (double p[4], double x)
{
  return p[1] + 0.5 * x * (p[2] - p[0] +
			   x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
				x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

double inline
bicubicInterpolate (double p[4][4], double x, double y)
{
  double arr[4];
  if (x < 0 || x > 1 || y < 0 || y > 1)
    abort ();
  /*x+=1;
     y+=1; */
  arr[0] = cubicInterpolate (p[0], y);
  arr[1] = cubicInterpolate (p[1], y);
  arr[2] = cubicInterpolate (p[2], y);
  arr[3] = cubicInterpolate (p[3], y);
  return cubicInterpolate (arr, x);
}



/* Mult specify how much one should multiply, add how much add
   and keep how much keep in the color.  */
struct pattern
{
  double mult[256][256][3];
  double add[256][256][3];
};

struct pattern pattern;

/* This should render the viewing screen, but because I though the older Thames screen was used
   it is wrong: it renders color dots rather than diagonal squares.
   It is not used anymore since I implemented better rendering algorithm.  */

/* The pattern is sqare.  In the center there is green circle
   of diameter DG, on corners there are red circles of diameter D  
   RR is a blurring radius.  */
#define D 70
#define DG 70

#define RR 2048
static void
init_render_pattern ()
{
  int xx, yy;
  for (xx = 0; xx < 256; xx++)
    for (yy = 0; yy < 256; yy++)
      {
	int d11 = xx * xx + yy * yy;
	int d21 = (256 - xx) * (256 - xx) + yy * yy;
	int d22 = (256 - xx) * (256 - xx) + (256 - yy) * (256 - yy);
	int d23 = xx * xx + (256 - yy) * (256 - yy);
	int dc = (128 - xx) * (128 - xx) + (128 - yy) * (128 - yy);
	int dl = xx * xx + (128 - yy) * (128 - yy);
	int dr = (256 - xx) * (256 - xx) + (128 - yy) * (128 - yy);
	int dt = (128 - xx) * (128 - xx) + (yy) * (yy);
	int db = (128 - xx) * (128 - xx) + (256 - yy) * (256 - yy);
	int d1, d3;

	pattern.add[xx][yy][0] = 0;
	pattern.add[xx][yy][1] = 0;
	pattern.add[xx][yy][2] = 0;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	if (d1 < (128 - DG))
	  {
	    pattern.mult[xx][yy][0] = RR * ((0.5 - d1 - DG) / (0.5 - DG));
	    if (pattern.mult[xx][yy][0] > 1)
	      pattern.mult[xx][yy][0] = 1;
	    pattern.mult[xx][yy][1] = 0;
	    pattern.mult[xx][yy][2] = 1 - pattern.mult[xx][yy][0];
	    continue;
	  }
	else if (d3 < (128 - D))
	  {
	    pattern.mult[xx][yy][1] = RR * ((0.5 - d3 - D) / (0.5 - D));
	    if (pattern.mult[xx][yy][1] > 1)
	      pattern.mult[xx][yy][1] = 1;
	    pattern.mult[xx][yy][2] = 0;
	    pattern.mult[xx][yy][2] = 1 - pattern.mult[xx][yy][1];
	    continue;
	  }
	else
	  {
	    pattern.mult[xx][yy][0] = 0;
	    pattern.mult[xx][yy][1] = 0;
	    pattern.mult[xx][yy][2] = 1;
	  }
      }
}

/* This computes the grid displayed by UI.  */

static void
init_preview_pattern ()
{
  int xx, yy;
  for (xx = 0; xx < 256; xx++)
    for (yy = 0; yy < 256; yy++)
      {
	int d11 = xx * xx + yy * yy;
	int d21 = (256 - xx) * (256 - xx) + yy * yy;
	int d22 = (256 - xx) * (256 - xx) + (256 - yy) * (256 - yy);
	int d23 = xx * xx + (256 - yy) * (256 - yy);
	int dc = (128 - xx) * (128 - xx) + (128 - yy) * (128 - yy);
	int dl = xx * xx + (128 - yy) * (128 - yy);
	int dr = (256 - xx) * (256 - xx) + (128 - yy) * (128 - yy);
	int dt = (128 - xx) * (128 - xx) + (yy) * (yy);
	int db = (128 - xx) * (128 - xx) + (256 - yy) * (256 - yy);
	int d1, d3;

	d1 = sqrt (fmin (d11, fmin (d21, fmin (d22, fmin (d23, dc)))));
	d3 = sqrt (fmin (dl, fmin (dr, fmin (dt, db))));
	pattern.add[xx][yy][0] = 0;
	pattern.add[xx][yy][1] = 0;
	pattern.add[xx][yy][2] = 0;
	pattern.mult[xx][yy][0] = 1;
	pattern.mult[xx][yy][1] = 1;
	pattern.mult[xx][yy][2] = 1;
	if (d1 < 30)
	  {
	    pattern.add[xx][yy][0] = 0.5 * (maxval - 1);
	    pattern.add[xx][yy][1] = 0;
	    pattern.add[xx][yy][2] = 0;
	    pattern.mult[xx][yy][0] = 0.5;
	    pattern.mult[xx][yy][1] = 0.25;
	    pattern.mult[xx][yy][2] = 0.25;
	    continue;
	  }
	else if (d3 < 30)
	  {
	    pattern.add[xx][yy][0] = 0;
	    pattern.add[xx][yy][1] = 0.5 * (maxval - 1);
	    pattern.add[xx][yy][2] = 0;
	    pattern.mult[xx][yy][0] = 0.25;
	    pattern.mult[xx][yy][1] = 0.5;
	    pattern.mult[xx][yy][2] = 0.25;
	    continue;
	  }
	else
	  {
	    if (xx < 10 || xx > 256 - 10 || yy < 10 || yy > 256 - 10)
	      {
		pattern.add[xx][yy][0] = 0;
		pattern.add[xx][yy][1] = 0;
		pattern.add[xx][yy][2] = 0.5 * (maxval - 1);
		pattern.mult[xx][yy][0] = 0.25;
		pattern.mult[xx][yy][1] = 0.25;
		pattern.mult[xx][yy][2] = 0.5;
	      }
	  }
      }
}


typedef struct _Data Data;
struct _Data
{
  GtkWidget *save;
  /*GtkImage *bigimage; */
  GtkImage *smallimage;
  GdkPixbuf *bigpixbuf;
  GdkPixbuf *smallpixbuf;
  GtkWidget *maindisplay_scroll;
  GtkWidget *image_viewer;
  GtkSpinButton *x1, *y1, *x2, *y2, *xdpi, *ydpi;
};
Data data;

G_MODULE_EXPORT void cb_press (GtkImage * image, GdkEventButton * event,
			       Data * data);
G_MODULE_EXPORT gboolean
cb_delete_event (GtkWidget * window, GdkEvent * event, Data * data)
{
  gint response = 1;

  /* Run dialog */
  /*response = gtk_dialog_run( GTK_DIALOG( data->quit ) );
     gtk_widget_hide( data->quit ); */

  return (1 != response);
}

G_MODULE_EXPORT void
cb_show_about (GtkButton * button, Data * data)
{
  /* Run dialog */
  /*gtk_dialog_run( GTK_DIALOG( data->about ) );
     gtk_widget_hide( data->about ); */
}

/* Load the input file. */

static void
openimage (int *argc, char **argv)
{
  FILE *in;
  pgm_init (argc, argv);
  ppm_init (argc, argv);
  in = fopen (argv[1], "r");
  if (!in)
    {
      perror (argv[1]);
      exit (1);
    }
  graydata = pgm_readpgm (fopen (argv[1], "r"), &xsize, &ysize, &maxval);
  maxval++;
}

/* Get values displayed in the UI.  */

static void
getvals (void)
{
  current.xstart = gtk_spin_button_get_value (data.x1);
  current.ystart = gtk_spin_button_get_value (data.y1);
  current.xend = gtk_spin_button_get_value (data.x2);
  current.yend = gtk_spin_button_get_value (data.y2);
  current.xm = 1 / (gtk_spin_button_get_value (data.xdpi) / 1000);
  current.ym = 1 / (gtk_spin_button_get_value (data.ydpi) / 1000);
  /*printf ("%lf %lf %lf %lf %lf %lf %lf\n", current.xstart, current.ystart, current.xend, current.yend, num,
	  current.xm, current.ym);*/
}

/* Set values displayed by the UI.  */

static void
setvals (void)
{
  initialized = 0;
  gtk_spin_button_set_value (data.x1, current.xstart);
  gtk_spin_button_set_value (data.y1, current.ystart);
  gtk_spin_button_set_value (data.x2, current.xend);
  gtk_spin_button_set_value (data.y2, current.yend);
  gtk_spin_button_set_value (data.xdpi, (1 / current.xm) * 1000 + 0.00000005);
  gtk_spin_button_set_value (data.ydpi, (1 / current.ym) * 1000 + 0.00000005);
  initialized = 1;
}

/* Render image into the main window.  */
static void
cb_image_annotate (GtkImageViewer * imgv,
		   GdkPixbuf * pixbuf,
		   gint shift_x,
		   gint shift_y,
		   gdouble scale_x, gdouble scale_y, gpointer user_data)
{
  int img_width = gdk_pixbuf_get_width (pixbuf);
  int img_height = gdk_pixbuf_get_height (pixbuf);
  int row_stride = gdk_pixbuf_get_rowstride (pixbuf);
  int pix_stride = 4;
  guint8 *buf = gdk_pixbuf_get_pixels (pixbuf);
  int col_idx, row_idx;

  if (shift_x < scale_x * 2 || shift_y < scale_x * 2)
    {
      gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER
					    (data.image_viewer), 4.0, 4.0, 64,
					    64);
      return;
    }
  if (shift_y < scale_x * 2)
    abort ();
  assert (scale_x == scale_y);
  bigrender (shift_x, shift_y, scale_x, pixbuf);
}

int setcenter;

/* Handle all the magic keys.  */
static gint
cb_key_press_event (GtkWidget * widget, GdkEventKey * event)
{
  gint k = event->keyval;

  if (k == 'c')
    setcenter = 1;
  if (k == 'q')
    {
      save_parameters ();
      current.ys += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'w')
    {
      save_parameters ();
      current.ys -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'a')
    {
      save_parameters ();
      current.xs += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 's')
    {
      save_parameters ();
      current.xs -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'e')
    {
      save_parameters ();
      current.ys2 += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'r')
    {
      save_parameters ();
      current.ys2 -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'd')
    {
      save_parameters ();
      current.xs2 += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'f')
    {
      save_parameters ();
      current.xs2 -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 't')
{
      save_parameters ();
    current.xs=current.ys=current.xs2=current.ys2=0;
      display_scheduled = 1;
}
  if (k == 'u')
    {
      undo_parameters ();	
      setvals ();
      display_scheduled = 1;
    }
  if (k == 'U')
    {
      redo_parameters ();
      setvals ();
      display_scheduled = 1;
    }

  return FALSE;
}


/* Initialize the GUI.  */
static GtkWidget *
initgtk (int *argc, char **argv)
{
  GtkBuilder *builder;
  GtkWidget *window;
  GtkWidget *image_viewer, *scrolled_win;

  gtk_init (argc, &argv);

  /* Create builder and load interface */
  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, "barveni.glade", NULL))
    {
      fprintf (stderr, "Can not open barveni.glade\n");
      exit (1);
    }

  /* Obtain widgets that we need */
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  data.save = GTK_WIDGET (gtk_builder_get_object (builder, "save"));
  /*data.bigimage = GTK_IMAGE (gtk_builder_get_object (builder, "bigimage")); */
  data.smallimage =
    GTK_IMAGE (gtk_builder_get_object (builder, "smallimage"));
  data.maindisplay_scroll =
    GTK_WIDGET (gtk_builder_get_object (builder, "maindisplay-scroll"));

  /* Add image_viewer.  */
  image_viewer = gtk_image_viewer_new (NULL);
  g_signal_connect (image_viewer,
		    "image-annotate", G_CALLBACK (cb_image_annotate), NULL);

  gtk_signal_connect (GTK_OBJECT (image_viewer), "key_press_event",
		      GTK_SIGNAL_FUNC (cb_key_press_event), NULL);
  /*gtk_signal_connect (GTK_OBJECT(image_viewer),     "button_press_event",
     GTK_SIGNAL_FUNC(cb_press), NULL); */
  data.image_viewer = image_viewer;

  gtk_container_add (GTK_CONTAINER (data.maindisplay_scroll), image_viewer);

  gtk_widget_show (image_viewer);

  // Set the scroll region and zoom range
  gtk_image_viewer_set_scroll_region (GTK_IMAGE_VIEWER (image_viewer),
				      20, 20, xsize - 20, ysize - 20);
  gtk_image_viewer_set_zoom_range (GTK_IMAGE_VIEWER (image_viewer), 2, 64);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (image_viewer), 4.0,
					4.0, 64, 64);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit (GTK_IMAGE_VIEWER (image_viewer));
  data.x1 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "x1"));
  data.y1 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y1"));
  data.x2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "x2"));
  data.y2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y2"));
  data.xdpi = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "xdpi"));
  data.ydpi = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "ydpi"));
  /*data.about = GTK_WIDGET( gtk_builder_get_object( builder, "aboutdialog1" ) ); */

  /* Connect callbacks */
  gtk_builder_connect_signals (builder, &data);

  /* Destroy builder */
  g_object_unref (G_OBJECT (builder));
  return window;
}

/* During rendering we use 2x2 transformation matrix to represent the mapping from image to screen
   and back.  */

struct transformation_data
{
  /* Number of repetitions of the screen pattern along the edge (xstart,xstart),(xend,yend)  */
  double num;
  /* image->screen translation and rotation matrix.  */
  double m1, m2, m3, m4;
  /* screen->image translation and rotation matrix (an inverse of above).  */
  double im1, im2, im3, im4;
} transform_data;

static inline void
init_transformation_data (struct transformation_data *trans)
{
  double a, b, c, d;
  double ox,oy;
  trans->num = sqrt ((current.xend - current.xstart) * (current.xend - current.xstart) * current.xm * current.xm
		     + (current.yend - current.ystart) * (current.yend - current.ystart) * current.ym * current.ym)
	       / (8.750032);	/* 8.75 pixels per pattern in the LOC 1000DPI scans.  */
  ox = (current.xend - current.xstart) * current.xm / (double) trans->num;
  oy = (current.yend - current.ystart) * current.ym / (double) trans->num;
  trans->im1 = ox;

  /* Finaly->image translation and rotation matrix.  */
  trans->im1 = a = ox / current.xm;
  trans->im2 = b = -oy / current.xm;
  trans->im3 = c = oy / current.ym;
  trans->im4 = d = ox / current.ym;

  /* Inverse image->finlay.  */
  trans->m1 = d / (a * d - b * c);
  trans->m2 = -b / (a * d - b * c);
  trans->m3 = -c / (a * d - b * c);
  trans->m4 = a / (a * d - b * c);
}

/* Transform coordinates of screen to image data.  */

static inline void
finlay_to_image (struct transformation_data *trans, double x, double y,
		 double *xp, double *yp)
{
  *xp = x * trans->im1 + y * trans->im2;
  *yp = x * trans->im3 + y * trans->im4;
  *xp *= 1+(current.ys)* (*yp+current.ystart) + (current.ys2) * (ysize - *yp - current.ystart);
  *yp *= 1+(current.xs)* (*xp+current.xstart) + (current.xs2) * (xsize - *xp - current.xstart);
  *xp += current.xstart;
  *yp += current.ystart;
}

/* Inverse transformation.
   FIXME: This is clearly missing xs/ys/xs2/ys2 handling.  */

static inline void
image_to_finlay (struct transformation_data *trans, double x, double y,
		 double *xp, double *yp)
{
  double px = ((x) - current.xstart);
  double py = ((y) - current.ystart);
  *xp = (px) * trans->m1 + (py) * trans->m2;
  *yp = (px) * trans->m3 + (py) * trans->m4;
}

/* Apply pattern to a given pixel.  */

static inline void
handle_pixel (struct transformation_data *trans, double scale,
	      double x, double y,
	      double graydata, double factor, double *r, double *g, double *b)
{
  double gg, rr, bb;
  double xx, yy;
  int ix, iy;

  image_to_finlay (trans, x / scale, y / scale, &xx, &yy);
  ix = (long long) (xx * 256) & 255;
  iy = (long long) (yy * 256) & 255;
  graydata *= factor;
  *r += graydata * pattern.mult[ix][iy][1] + pattern.add[ix][iy][1];
  *g += graydata * pattern.mult[ix][iy][0] + pattern.add[ix][iy][0];
  *b += graydata * pattern.mult[ix][iy][2] + pattern.add[ix][iy][2];
}

/* Compute one row of the color image.
   This is the basic rendering routing which is no longer used.  */

static void
compute_row (double y, gray * data, pixel * outrow, int size, int offset,
	     double scale)
{
  int x;
  double graymul = 1, basemul = 0;
  double valscale = 65536 / 8 / (double) maxval;
  struct transformation_data trans;

  init_transformation_data (&trans);
  for (x = 0; x < size; x++)
    {
      int rr, gg, bb;
      double r, g, b;

      r = g = b = 0;

      handle_pixel (&trans , scale, offset + x - 1.0 / 3, y - 1.0 / 3,
		    data[x], 0.5, &r, &g, &b);
      handle_pixel (&trans , scale, offset + x - 1.0 / 3, y, data[x],
		    1, &r, &g, &b);
      handle_pixel (&trans , scale, offset + x - 1.0 / 3, y - 1.0 / 3,
		    data[x], 0.5, &r, &g, &b);

      handle_pixel (&trans , scale, offset + x, y - 1.0 / 3, data[x],
		    1, &r, &g, &b);
      handle_pixel (&trans , scale, offset + x, y, data[x], 2, &r, &g,
		    &b);
      handle_pixel (&trans , scale, offset + x, y - 1.0 / 3, data[x],
		    1, &r, &g, &b);

      handle_pixel (&trans , scale, offset + x + 1.0 / 3, y - 1.0 / 3,
		    data[x], 0.5, &r, &g, &b);
      handle_pixel (&trans , scale, offset + x + 1.0 / 3, y, data[x],
		    1, &r, &g, &b);
      handle_pixel (&trans , scale, offset + x + 1.0 / 3, y - 1.0 / 3,
		    data[x], 0.5, &r, &g, &b);

      outrow[x].r = r * valscale;
      outrow[x].g = g * valscale;
      outrow[x].b = b * valscale;
    }
}

/* Compute one row.  This is used to draw the small preview winow. */

static void
compute_row_fast (double y, gray * data, pixel * outrow, int size, int offset,
		  double scale)
{
  int x;
  double graymul = 1, basemul = 0;
  double valscale = 65536 / (double) maxval;
  struct transformation_data trans;

  init_transformation_data (&trans);
  for (x = 0; x < size; x++)
    {
      int rr, gg, bb;
      double r, g, b;

      r = g = b = 0;

      handle_pixel (&trans , scale, offset + x, y, data[x], 1, &r, &g,
		    &b);

      outrow[x].r = r * valscale;
      outrow[x].g = g * valscale;
      outrow[x].b = b * valscale;
    }
}

/* Uused to draw into the previews.  Differs by data type.  */

static inline void
my_putpixel2 (guint8 * pixels, int rowstride, int x, int y, int r, int g,
	      int b)
{
  *(pixels + y * rowstride + x * 4) = r;
  *(pixels + y * rowstride + x * 4 + 1) = g;
  *(pixels + y * rowstride + x * 4 + 2) = b;
}

static inline void
my_putpixel (guchar * pixels, int rowstride, int x, int y, int r, int g,
	     int b)
{
  *(pixels + y * rowstride + x * 3) = r;
  *(pixels + y * rowstride + x * 3 + 1) = g;
  *(pixels + y * rowstride + x * 3 + 2) = b;
}

/* Determine grayscale value at a given position in the screen coordinates.  */

static double
sample (struct transformation_data *trans, double x, double y)
{
  double xp, yp;
  int sx, sy;
  double p[4][4];
  double val;
  finlay_to_image (trans, x, y, &xp, &yp);
  sx = xp, sy = yp;

  if (xp < 2 || xp >= xsize - 2 || yp < 2 || yp >= ysize - 2)
    return 0;
  p[0][0] = graydata[sy - 1][sx - 1];
  p[1][0] = graydata[sy - 1][sx - 0];
  p[2][0] = graydata[sy - 1][sx + 1];
  p[3][0] = graydata[sy - 1][sx + 2];
  p[0][1] = graydata[sy - 0][sx - 1];
  p[1][1] = graydata[sy - 0][sx - 0];
  p[2][1] = graydata[sy - 0][sx + 1];
  p[3][1] = graydata[sy - 0][sx + 2];
  p[0][2] = graydata[sy + 1][sx - 1];
  p[1][2] = graydata[sy + 1][sx - 0];
  p[2][2] = graydata[sy + 1][sx + 1];
  p[3][2] = graydata[sy + 1][sx + 2];
  p[0][3] = graydata[sy + 2][sx - 1];
  p[1][3] = graydata[sy + 2][sx - 0];
  p[2][3] = graydata[sy + 2][sx + 1];
  p[3][3] = graydata[sy + 2][sx + 2];
  val = bicubicInterpolate (p, xp - sx, yp - sy);
  if (val < 0)
    val = 0;
  if (val > maxval - 1)
    val = maxval - 1;
  return val;
}

#define NBLUE 8			/* We need 6 rows of blue.  */
#define NRED 8			/* We need 7 rows of the others.  */

inline int
getmatrixsample (double **sample, int *shift, int pos, int xp, int x, int y)
{
  int line = (pos + NRED + x + y) % NRED;
  return sample[line][((xp + y * 2 - x * 2) - shift[line]) / 4];
}

/* This renders the small preview widget.   */

static void
previewrender (GdkPixbuf ** pixbuf)
{
  int x, y;
  int my_xsize, my_ysize, rowstride;
  guint8 *pixels;
  int xshift = 0, yshift = 0;
  int scale = 1;
  struct transformation_data trans;

  init_transformation_data (&trans);

  xshift = (current.xstart * trans.num / (current.xend - current.xstart));
  yshift = (current.ystart * trans.num / (current.xend - current.xstart));
  my_xsize = (xsize * trans.num / (current.xend - current.xstart));
  my_ysize = (ysize * trans.num / (current.xend - current.xstart));

  gdk_pixbuf_unref (*pixbuf);
  *pixbuf =
    gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, (my_xsize + scale - 1) / scale,
		    (my_ysize + scale - 1) / scale);

  pixels = gdk_pixbuf_get_pixels (*pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (*pixbuf);

  for (y = 0; y < my_ysize; y += scale)
    for (x = 0; x < my_xsize; x += scale)
      {
	double dx = x + 0.5 - xshift, dy = y + 0.5 - yshift;
	double avg;
	double green =
	  sample (&trans, dx, dy) + sample (&trans, dx + 1,
					    dy + 1) + sample (&trans, dx,
							      dy + 1) +
	  sample (&trans, dx + 1, dy) + sample (&trans, dx + 0.5, dy + 0.5);
	double red =
	  sample (&trans, dx + 0.5, dy) + sample (&trans, dx,
						  dy + 0.5) + sample (&trans,
								      dx + 1,
								      dy +
								      0.5) +
	  sample (&trans, dx + 0.5, dy + 1);
	double blue =
	  sample (&trans, dx + 0.25, dy + 0.25) + sample (&trans, dx + 0.75,
							  dy + 0.25) +
	  sample (&trans, dx + 0.25,
		  dy + 0.75) + sample (&trans, dx + 0.75, dy + 0.75);
	green = green * 51;
	red = red * 64;
	blue = blue * 64;
	avg = (red + green + blue) / 3;
	red = avg + (red - avg) * 5;
	if (red < 0)
	  red = 0;
	if (red >= maxval * 256)
	  red = maxval * 256 - 1;
	green = avg + (green - avg);
	if (green < 0)
	  green = 0;
	if (green >= maxval * 256)
	  green = maxval * 256 - 1;
	blue = avg + (blue - avg);
	if (blue < 0)
	  blue = 0;
	if (blue >= maxval * 256)
	  blue = maxval * 256 - 1;
	my_putpixel (pixels, rowstride, x / scale, y / scale, red / maxval,
		     green / maxval, blue / maxval);
      }
}

struct samples
{
  int xshift, yshift;
  int xsize, ysize;
  double *redsample[8];
  double *greensample[8];
  double *bluesample[NBLUE];
  int bluepos[NBLUE];
  int redpos[8];
  int redshift[8];
  int greenpos[8];
  int greenshift[8];
  int redp, greenp, bluep;
  int scale;
};

static void
init_finalrender (struct samples *samples, int scale)
{
  int i;
  struct transformation_data trans;

  init_transformation_data (&trans);
  samples->xshift = (current.xstart * trans.num / (current.xend - current.xstart));
  samples->yshift = (current.ystart * trans.num / (current.xend - current.xstart));
  samples->xsize = (xsize * trans.num / (current.xend - current.xstart));
  samples->ysize = (ysize * trans.num / (current.xend - current.xstart));
  samples->scale = scale;
  for (i = 0; i < 8; i++)
    {
      samples->redsample[i] = malloc (sizeof (double) * samples->xsize);
      samples->greensample[i] = malloc (sizeof (double) * samples->xsize);
    }
  for (i = 0; i < NBLUE; i++)
    samples->bluesample[i] = malloc (sizeof (double) * samples->xsize * 2);
  samples->bluep = samples->redp = samples->greenp = 0;
}

static void
finalrender_row (int y, pixel ** outrow, struct samples *samples)
{
  double **redsample = samples->redsample;
  double **greensample = samples->greensample;
  double **bluesample = samples->bluesample;
  int *bluepos = samples->bluepos;
  int *redpos = samples->redpos;
  int *redshift = samples->redshift;
  int *greenpos = samples->greenpos;
  int *greenshift = samples->greenshift;
  int x;
  int sx;
  int sy;
  int scale = samples->scale;
  struct transformation_data trans;

  init_transformation_data (&trans);

  if (y % 4 == 0)
    {
      for (x = 0; x < samples->xsize; x++)
	greensample[samples->greenp][x] =
	  sample (&trans, x - samples->xshift,
		  (y - samples->yshift * 4) / 4.0);
      samples->greenpos[samples->greenp] = y;
      greenshift[samples->greenp] = 0;
      samples->greenp++;
      samples->greenp %= 8;

      for (x = 0; x < samples->xsize; x++)
	redsample[samples->redp][x] =
	  sample (&trans, x - samples->xshift + 0.5,
		  (y - samples->yshift * 4) / 4.0);
      redpos[samples->redp] = y;
      redshift[samples->redp] = 2;
      samples->redp++;
      samples->redp %= 8;
    }
  if (y % 4 == 2)
    {
      for (x = 0; x < samples->xsize; x++)
	redsample[samples->redp][x] =
	  sample (&trans, x - samples->xshift,
		  (y - samples->yshift * 4) / 4.0);
      redpos[samples->redp] = y;
      redshift[samples->redp] = 0;
      samples->redp++;
      samples->redp %= 8;

      for (x = 0; x < samples->xsize; x++)
	greensample[samples->greenp][x] =
	  sample (&trans, x - samples->xshift + 0.5,
		  (y - samples->yshift * 4) / 4.0);
      samples->greenpos[samples->greenp] = y;
      greenshift[samples->greenp] = 2;
      samples->greenp++;
      samples->greenp %= 8;
    }
  if (y % 4 == 1 || y % 4 == 3)
    {
      bluepos[samples->bluep] = y;
      for (x = 0; x < samples->xsize; x++)
	{
	  bluesample[samples->bluep][x * 2] =
	    sample (&trans, x - samples->xshift + 0.25,
		    (y - samples->yshift * 4) / 4.0);
	  bluesample[samples->bluep][x * 2 + 1] =
	    sample (&trans, x - samples->xshift + 0.75,
		    (y - samples->yshift * 4) / 4.0);
	}
      samples->bluep++;
      samples->bluep %= NBLUE;
    }
  if (y > 8 * 4)
    {
#define OFFSET  7
      int rendery = y - OFFSET;
      int bluestart =
	(samples->bluep + NBLUE - ((OFFSET + 1) / 2 + 2)) % NBLUE;
      double bluey;
      int redcenter = (samples->redp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int greencenter = (samples->greenp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int xx, yy;
      printf ("%i %i\n", y, scale);

      if (bluepos[(bluestart + 2) % NBLUE] == rendery)
	bluestart = (bluestart + 1) % NBLUE;
      /*fprintf (stderr, "baf:bp:%i rendery:%i:%i %i %i %f\n", bluepos[(bluestart + 1) % NBLUE], rendery,y, bluey, bluep, bluey); */
      assert (bluepos[(bluestart + 1) % NBLUE] <= rendery);
      assert (bluepos[(bluestart + 2) % NBLUE] > rendery);

      if (redpos[(redcenter + 1) % NRED] == rendery)
	redcenter = (redcenter + 1) % NRED;
      assert (redpos[(redcenter) % NBLUE] <= rendery);
      assert (redpos[(redcenter + 1) % NBLUE] > rendery);

      if (greenpos[(greencenter + 1) % NRED] == rendery)
	greencenter = (greencenter + 1) % NRED;
      assert (greenpos[(greencenter) % NBLUE] <= rendery);
      assert (greenpos[(greencenter + 1) % NBLUE] > rendery);
      for (yy = 0; yy < scale; yy++)
	{
	  bluey =
	    (rendery + ((double) yy) / scale -
	     bluepos[(bluestart + 1) % NBLUE]) / 2.0;
	  for (x = 8; x < samples->xsize * 4; x++)
	    for (xx = 0; xx < scale; xx++)
	      {
		double p[4][4];
		double red, green, blue;
		double xo, yo;
		int np;
		int bluex = (x - 1) / 2;
		int val;

		p[0][0] = bluesample[bluestart][bluex - 1];
		p[1][0] = bluesample[bluestart][bluex];
		p[2][0] = bluesample[bluestart][bluex + 1];
		p[3][0] = bluesample[bluestart][bluex + 2];
		p[0][1] = bluesample[(bluestart + 1) % NBLUE][bluex - 1];
		p[1][1] = bluesample[(bluestart + 1) % NBLUE][bluex];
		p[2][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 1];
		p[3][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 2];
		p[0][2] = bluesample[(bluestart + 2) % NBLUE][bluex - 1];
		p[1][2] = bluesample[(bluestart + 2) % NBLUE][bluex];
		p[2][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 1];
		p[3][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 2];
		p[0][3] = bluesample[(bluestart + 3) % NBLUE][bluex - 1];
		p[1][3] = bluesample[(bluestart + 3) % NBLUE][bluex];
		p[2][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 1];
		p[3][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 2];
		xo = (double) (x + ((double) xx) / scale - 1) / 2;
		blue = bicubicInterpolate (p, xo - (int) xo, bluey);
		if (blue < 0)
		  blue = 0;
		if (blue > maxval - 1)
		  blue = maxval - 1;
		{
		  int sx = ((x - redshift[redcenter]) + 2) / 4;
		  int dx = (x - redshift[redcenter]) - sx * 4;
		  int dy = rendery - redpos[redcenter];
		  int currcenter = redcenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (redcenter + NRED - 1) % NRED;
		      sx = ((x - redshift[currcenter]) + 2) / 4;
		    }
		  red = redsample[currcenter][sx];

		  /*red = getmatrixsample (redsample, redshift, currcenter, sx * 4 + redshift[currcenter], 0, 0); */
		  sx = sx * 4 + redshift[currcenter];
		  p[0][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     -1);
		  p[0][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     0);
		  p[0][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     1);
		  p[0][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     2);
		  p[1][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     -1);
		  p[1][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     0);
		  p[1][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     1);
		  p[1][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     2);
		  p[2][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     -1);
		  p[2][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     0);
		  p[2][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     1);
		  p[2][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     2);
		  p[3][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     -1);
		  p[3][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     0);
		  p[3][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     1);
		  p[3][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     2);
		  distx = x - sx;
		  disty = rendery - redpos[currcenter];
		  red =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		  if (red < 0)
		    red = 0;
		  if (red > maxval - 1)
		    red = maxval - 1;
		}
		{
		  int sx = ((x - greenshift[greencenter]) + 2) / 4;
		  int dx = (x - greenshift[greencenter]) - sx * 4;
		  int dy = rendery - greenpos[greencenter];
		  int currcenter = greencenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (greencenter + NRED - 1) % NRED;
		      sx = ((x - greenshift[currcenter]) + 2) / 4;
		    }
		  green = greensample[currcenter][sx];

		  /*green = getmatrixsample (greensample, greenshift, currcenter, sx * 4 + greenshift[currcenter], 0, 0); */
		  sx = sx * 4 + greenshift[currcenter];
		  p[0][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, -1);
		  p[0][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 0);
		  p[0][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 1);
		  p[0][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 2);
		  p[1][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, -1);
		  p[1][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 0);
		  p[1][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 1);
		  p[1][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 2);
		  p[2][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, -1);
		  p[2][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 0);
		  p[2][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 1);
		  p[2][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 2);
		  p[3][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, -1);
		  p[3][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 0);
		  p[3][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 1);
		  p[3][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 2);
		  distx = x - sx;
		  disty = rendery - greenpos[currcenter];
		  green =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		  if (green < 0)
		    green = 0;
		  if (green > maxval - 1)
		    green = maxval - 1;
		}


#if 1
		val =
		  sample (&trans, (x - samples->xshift * 4 +
				   xx / (double) scale) / 4.0,
			  (rendery - samples->yshift * 4 +
			   yy / (double) scale) / 4.0) * 256;
		if (red != 0 || green != 0 || blue != 0)
		  {
		    double sum = (red + green + blue) / 3;
		    red = red * val / sum;
		    green = green * val / sum;
		    blue = blue * val / sum;
		  }
		else
		  red = green = blue = val;
#else
		red *= 256;
		green *= 256;
		blue *= 256;
#endif
		if (red > 65536 - 1)
		  red = 65536 - 1;
		if (red < 0)
		  red = 0;
		if (green > 65536 - 1)
		  green = 65536 - 1;
		if (green < 0)
		  green = 0;
		if (blue > 65536 - 1)
		  blue = 65536 - 1;
		if (blue < 0)
		  blue = 0;
		outrow[yy][(x * scale + xx)].r = red;
		outrow[yy][(x * scale + xx)].g = green;
		outrow[yy][(x * scale + xx)].b = blue;
	      }
	}
    }
}

static void
bigrender (int xoffset, int yoffset, double bigscale, GdkPixbuf * bigpixbuf)
{
  int bigrowstride, smallrowstride;
  int x, y;
  guint8 *bigpixels;
  int smallxsize = (xsize) / SCALE + 1;
  int smallysize = (ysize) / SCALE + 1;
  int maxr = 1, maxg = 1, maxb = 1;
  gray *biggraydata = malloc (sizeof (gray) * xsize * bigscale);;
  int pxsize;
  int pysize;
  pixel *outrow;

  bigrowstride = gdk_pixbuf_get_rowstride (bigpixbuf);
  bigpixels = gdk_pixbuf_get_pixels (bigpixbuf);
  pxsize = gdk_pixbuf_get_width (bigpixbuf);
  pysize = gdk_pixbuf_get_height (bigpixbuf);
  outrow = ppm_allocrow (pxsize);
  /*printf ("Bigrender %i %i %i\n", pxsize, pysize, bigrowstride); */
  init_preview_pattern ();
  if (xoffset < bigscale)
    xoffset = bigscale;
  if (yoffset < bigscale)
    yoffset = bigscale;
  for (y = 0; y < pysize; y++)
    {
      int sy = (y + yoffset) / bigscale;
      double py = (y + yoffset) / bigscale;
      double p[4];
      double arr[4];
      double lx;
      int offset = 0;
      py = py - (int) py;
      int sx = xoffset / bigscale;

      p[0] = graydata[sy - 1][sx - 1];
      p[1] = graydata[sy - 0][sx - 1];
      p[2] = graydata[sy + 1][sx - 1];
      p[3] = graydata[sy + 2][sx - 1];
      arr[0] = cubicInterpolate (p, py);
      p[0] = graydata[sy - 1][sx];
      p[1] = graydata[sy - 0][sx];
      p[2] = graydata[sy + 1][sx];
      p[3] = graydata[sy + 2][sx];
      arr[1] = cubicInterpolate (p, py);
      p[0] = graydata[sy - 1][sx + 1];
      p[1] = graydata[sy - 0][sx + 1];
      p[2] = graydata[sy + 1][sx + 1];
      p[3] = graydata[sy + 2][sx + 1];
      arr[2] = cubicInterpolate (p, py);
      p[0] = graydata[sy - 1][sx + 2];
      p[1] = graydata[sy - 0][sx + 2];
      p[2] = graydata[sy + 1][sx + 2];
      p[3] = graydata[sy + 2][sx + 2];
      arr[3] = cubicInterpolate (p, py);
      lx = sx;
      for (x = 0; x < pxsize; x++)
	{
	  int sx = (x + xoffset) / bigscale;
	  double px = (x + xoffset) / bigscale;
	  double val;
	  px = px - (int) px;
	  if (lx != sx)
	    {
	      arr[0] = arr[1];
	      arr[1] = arr[2];
	      arr[2] = arr[3];
	      p[0] = graydata[sy - 1][sx + 2];
	      p[1] = graydata[sy - 0][sx + 2];
	      p[2] = graydata[sy + 1][sx + 2];
	      p[3] = graydata[sy + 2][sx + 2];
	      arr[3] = cubicInterpolate (p, py);
	      lx = sx;
	    }
	  val = cubicInterpolate (arr, px);
	  if (val < 0)
	    val = 0;
	  if (val > maxval - 1)
	    val = maxval - 1;
	  biggraydata[x] = val;
	}
      compute_row_fast (y + yoffset, biggraydata, outrow, pxsize, xoffset,
			bigscale);
      for (x = 0; x < pxsize; x++)
	{
	  int r = outrow[x].r / 256;
	  int g = outrow[x].g / 256;
	  int b = outrow[x].b / 256;
	  my_putpixel2 (bigpixels, bigrowstride, x, y, r, g, b);
	}
    }
  {
    cairo_surface_t *surface
      =
      cairo_image_surface_create_for_data (gdk_pixbuf_get_pixels (bigpixbuf),
					   CAIRO_FORMAT_RGB24,
					   pxsize,
					   pysize,
					   gdk_pixbuf_get_rowstride
					   (bigpixbuf));
    cairo_t *cr = cairo_create (surface);
    cairo_translate (cr, -xoffset, -yoffset);
    cairo_scale (cr, bigscale, bigscale);

    cairo_set_source_rgba (cr, 0, 0, 1.0, 0.5);
    cairo_arc (cr, current.xstart, current.ystart, 3, 0.0, 2 * G_PI);

    cairo_fill (cr);


    cairo_surface_destroy (surface);
    cairo_destroy (cr);

  }
  free (outrow);
  free (biggraydata);
}

static void
display ()
{
  gtk_image_viewer_redraw (data.image_viewer, 1);
  previewrender (&data.smallpixbuf);
  gtk_image_set_from_pixbuf (data.smallimage, data.smallpixbuf);
}

G_MODULE_EXPORT void
cb_redraw (GtkButton * button, Data * data)
{
  if (!initialized)
    return;
  getvals ();
  display ();
}

G_MODULE_EXPORT void
cb_press_small (GtkImage * image, GdkEventButton * event, Data * data)
{
  getvals ();
  printf ("Press small %i %i\n", event->x, event->y);
  if (event->button == 1)
    {
      offsetx =
	8 +
	(event->x) * xsize * bigscale /
	gdk_pixbuf_get_width (data->smallpixbuf);
      offsety =
	8 +
	(event->y) * ysize * bigscale /
	gdk_pixbuf_get_height (data->smallpixbuf);
    }
}

double xpress, ypress;
double xpress1, ypress1;
double pressxend, pressyend;
double pressxstart, pressystart;
bool button1_pressed;
bool button3_pressed;

G_MODULE_EXPORT void
cb_press (GtkImage * image, GdkEventButton * event, Data * data2)
{
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					&scale_x, &scale_y, &shift_x,
					&shift_y);
  if (!initialized)
    return;
  printf ("Press x:%i y:%i zoomx:%f zoomy:%f shiftx:%i shifty:%i \n",
	  (int) event->x, (int) event->y, (double) scale_x, (double) scale_y,
	  (int) shift_x, (int) shift_y);
  if (event->button == 1 && setcenter)
    {
      double newxstart;
      double newystart;
      newxstart = (event->x + shift_x) / scale_x;
      newystart = (event->y + shift_y) / scale_y;
      if (newxstart != current.xstart || newystart != current.ystart)
	{
	  current.xend += newxstart - current.xstart;
	  current.yend += newystart - current.ystart;
	  current.xstart = newxstart;
	  current.ystart = newystart;
	  setcenter = 0;
	  setvals ();
	  display_scheduled = true;
	}
    }
  pressxstart = current.xstart;
  pressystart = current.ystart;
  pressxend = current.xend;
  pressyend = current.yend;
  if (event->button == 1)
    {
      xpress1 = event->x;
      ypress1 = event->y;
      button1_pressed = true;
    }
  else if (event->button == 3)
    {
      xpress = (event->x + shift_x) / scale_x;
      ypress = (event->y + shift_y) / scale_y;
      button3_pressed = true;
    }
}

handle_drag (int x, int y, int button)
{
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER
					(data.image_viewer), &scale_x,
					&scale_y, &shift_x, &shift_y);
  if (button == 1)
    {
      double xoffset = (x - xpress1) / scale_x;
      double yoffset = (y - ypress1) / scale_y;
      if (current.xstart == pressxstart + xoffset && current.ystart == pressystart + yoffset)
	return;
      current.xend = pressxend + xoffset;
      current.yend = pressyend + yoffset;
      current.xstart = pressxstart + xoffset;
      current.ystart = pressystart + yoffset;
      setvals ();
      display_scheduled = true;
    }
  else if (button == 3)
    {
      double x1 = (xpress - current.xstart);
      double y1 = (ypress - current.ystart);
      double x2 = (x + shift_x) / scale_x - current.xstart;
      double y2 = (y + shift_y) / scale_y - current.ystart;
      double angle = atan2f (y2, x2) - atan2f (y1, x1);
      if (!angle)
	return;
      current.xend =
	current.xstart + (pressxend - current.xstart) * cos (angle) + (pressyend -
						       current.ystart) * sin (angle);
      current.yend =
	current.ystart + (pressxend - current.xstart) * sin (angle) + (pressyend -
						       current.ystart) * cos (angle);

      setvals ();
      display_scheduled = true;
    }
}

G_MODULE_EXPORT void
cb_release (GtkImage * image, GdkEventButton * event, Data * data2)
{
  handle_drag (event->x, event->y, event->button);
  save_parameters ();
  if (event->button == 1)
    button1_pressed = false;
  if (event->button == 3)
    button3_pressed = false;
}

G_MODULE_EXPORT void
cb_drag (GtkImage * image, GdkEventMotion * event, Data * data2)
{
  handle_drag (event->x, event->y,
	       button1_pressed ? 1 : button3_pressed ? 3 : 0);
}

#define RANGE 2
#define STEPS 201

G_MODULE_EXPORT void
cb_save (GtkButton * button, Data * data)
{
  pixel *outrow;
  pixel *outrows[16];
  FILE *out;
  int y;
  double xend2 = current.xend, yend2 = current.yend;
  int i;
  struct samples samples;
  int scale;
  out = fopen (paroname, "w");
  fprintf (out, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", current.xstart, current.ystart, current.xend, current.yend,
	   0.0, current.xm, current.ym,current.xs,current.ys,current.xs2,current.ys2);
  fclose (out);

#if 0
init_render_pattern ();
    {
      double angle = 0;
      char fname[256];
      printf ("Save %f\n",angle);
      outrow = ppm_allocrow (xsize);
      sprintf (fname,"out-%2.2f.pgm",angle);
      out = fopen (fname, "w");
      angle = angle*2*3.14/360;
      ppm_writeppminit (out, xsize, ysize-2, 65535, 0);
      for (y = 1; y < ysize-1; y++)
	{
	  compute_row (y,graydata[y],outrow, xsize, 0, 1);
	  ppm_writeppmrow (out, outrow, xsize, 65535, 0);
	}
      fclose (out);
    }
#endif


  scale = 4;
  init_finalrender (&samples, scale);
  out = fopen (oname, "w");
  assert (scale < 16);
  for (y = 0; y < scale; y++)
    outrows[y] = ppm_allocrow (samples.xsize * scale * 4);
  ppm_writeppminit (out, samples.xsize * scale * 4, samples.ysize * scale * 4,
		    65535, 0);
  for (y = 0; y < samples.ysize * 4; y++)
    {
      int yy;
      finalrender_row (y, outrows, &samples);
      for (yy = 0; yy < scale; yy++)
	ppm_writeppmrow (out, outrows[yy], samples.xsize * scale * 4, 65535,
			 0);
    }
  fclose (out);
  for (y = 0; y < scale; y++)
    free (outrows[y]);
}




int
main (int argc, char **argv)
{
  GtkWidget *window;
  double num;
  openimage (&argc, argv);
  oname = argv[2];
  paroname = argv[3];
  scanf ("%lf %lf %lf %lf %lf %lf %lf", &current.xstart, &current.ystart, &current.xend, &current.yend, &num,
	 &current.xm, &current.ym);
  window = initgtk (&argc, argv);
  setvals ();
  initialized = 1;
  data.smallpixbuf =
    gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, (xsize) / SCALE + 1,
		    (ysize) / SCALE + 1);
  /* Show main window and start main loop */
  gtk_widget_show (window);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					4.0, 4.0, 64, 64);
  gtk_image_viewer_redraw (data.image_viewer, 1);

  while (true)
    {
      if (display_scheduled)
	{
	  display ();
	  display_scheduled = false;
	}
      gtk_main_iteration_do (true);
      while (gtk_events_pending ())
	{
	  gtk_main_iteration_do (FALSE);
	}
    }

  return (0);
}
