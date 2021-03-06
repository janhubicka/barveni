#ifndef RENDER_H
#define RENDER_H
#include <math.h>
#include <algorithm>
#include "scr-to-img.h"
#include "imagedata.h"
#include "color.h"

/* Parameters of rendering algorithms.  */
struct DLL_PUBLIC render_parameters
{
  render_parameters()
  : gamma (2.2), presaturation (1), saturation (1.5), brightness (1), collection_threshold (0.8),
    mix_gamma (2.2), mix_red (0.3), mix_green (0.1), mix_blue (1),
    screen_blur_radius (1.3),
    color_model (3), gray_min (0), gray_max (255), precise (true),
    screen_compensation (true), adjust_luminosity (false)
  {}
  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).  */
  luminosity_t gamma;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of sharpness.  */
  luminosity_t presaturation;
  /* Saturation increase.  */
  luminosity_t saturation;
  /* Brightness adjustments.  */
  luminosity_t brightness;
  /* Threshold for collecting color information.  */
  luminosity_t collection_threshold;
  /* Parameters used to turn RGB data to grayscale:
     mix_gamma should be gamma of the scan, mix_red,green and blue
     are relative weights.  */
  luminosity_t mix_gamma, mix_red, mix_green, mix_blue;
  /* Radius (in image pixels) the screen should be blured.  */
  coord_t screen_blur_radius;
  /* If true apply color model of Finlay taking plate.  */
  int color_model;
  /* Gray range to boot to full contrast.  */
  int gray_min, gray_max;

  /* The following is used by interpolated rendering only.  */
  /* If true use precise data collection.  */
  bool precise;
  /* If true try to compensate for screen.  */
  bool screen_compensation;
  /* If true use luminosity from scan.  */
  bool adjust_luminosity;
};

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class DLL_PUBLIC render
{
public:
  render (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
  : m_img (img), m_params (rparam), m_data (img.data), m_maxval (img.data ? img.maxval : 65535), m_dst_maxval (dstmaxval)
  {
    m_scr_to_img.set_parameters (param);
  }
  ~render ();
  inline luminosity_t get_img_pixel (coord_t x, coord_t y);
  inline void get_img_rgb_pixel (coord_t x, coord_t y, luminosity_t *r, luminosity_t *g, luminosity_t *b);
  inline luminosity_t sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2);
  inline luminosity_t sample_scr_diag_square (coord_t xc, coord_t yc, coord_t s);
  inline luminosity_t sample_scr_square (coord_t xc, coord_t yc, coord_t w, coord_t h);
  inline luminosity_t fast_get_img_pixel (int x, int y);
  inline luminosity_t get_img_pixel_scr (coord_t x, coord_t y);
  coord_t pixel_size ();
  void precompute_all ();
  void precompute (luminosity_t, luminosity_t, luminosity_t, luminosity_t) {precompute_all ();}
  void precompute_img_range (luminosity_t, luminosity_t, luminosity_t, luminosity_t) {precompute_all ();}
    
  static const int num_color_models = 4;
  enum render_type_t
  {
    render_type_original,
    render_type_preview_grid,
    render_type_realistic,
    render_type_interpolated,
    render_type_combined,
    render_type_predictive,
    render_type_fast
  };
  DLL_PUBLIC static void render_tile (enum render_type_t render_type, scr_to_img_parameters &param, image_data &img, render_parameters &rparam,
				      bool color, unsigned char *pixels, int rowstride, int pixelbytes, int width, int height,
				      double xoffset, double yoffset, double step);
protected:
  inline luminosity_t get_data (int x, int y);
  inline luminosity_t get_data_red (int x, int y);
  inline luminosity_t get_data_green (int x, int y);
  inline luminosity_t get_data_blue (int x, int y);
  inline void set_color (luminosity_t, luminosity_t, luminosity_t, int *, int *, int *);
  inline void set_color_luminosity (luminosity_t, luminosity_t, luminosity_t, luminosity_t, int *, int *, int *);

  /* Scanned image.  */
  image_data &m_img;
  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  /* Rendering parameters.  */
  render_parameters &m_params;
  /* Grayscale we render from.  */
  unsigned short **m_data;
  /* Maximal value in m_data.  */
  int m_maxval;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input gray values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_lookup_table;
  /* Translates input rgb channel values into normalized range 0...1 gamma 1.  */
  luminosity_t *m_rgb_lookup_table;
  /* Translates back to gamma 2.  */
  luminosity_t *m_out_lookup_table;
  /* Color matrix.  */
  color_matrix m_color_matrix;
};

DLL_PUBLIC bool save_csp (FILE *f, scr_to_img_parameters &param, render_parameters &rparam);
DLL_PUBLIC bool load_csp (FILE *f, scr_to_img_parameters &param, render_parameters &rparam, const char **error);

/* Base class for renderes tha works in screen coordinates (so output image is
   geometrically corrected.  */
class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render (param, img, rparam, dstmaxval)
  {
    m_scr_to_img.get_range (m_img.width, m_img.height, &m_scr_xshift, &m_scr_yshift, &m_scr_width, &m_scr_height);
  }
  /* This returns screen coordinate width of rendered output.  */
  int get_width ()
  {
    return m_scr_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int get_height ()
  {
    return m_scr_height;
  }
protected:
  /* Rectangular section of the screen to which the whole image fits.

     The section is having dimensions scr_width x scr_height and will
     start at position (-scr_xshift, -scr_yshift).  */
  int m_scr_xshift, m_scr_yshift;
  int m_scr_width, m_scr_height;
};

/* Do no rendering of color screen.  */
class render_img : public render_to_scr
{
public:
  render_img (scr_to_img_parameters &param, image_data &img, render_parameters &rparam, int dstmaxval)
    : render_to_scr (param, img, rparam, dstmaxval), m_color (false)
  { }
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
  void inline render_pixel_img (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    luminosity_t gg, rr, bb;
    if (!m_color)
      rr = gg = bb = fast_get_img_pixel (x, y);
    else
      get_img_rgb_pixel (x, y, &rr, &gg, &bb);
    set_color (rr, gg, bb, r, g, b);
  }
  int inline render_raw_pixel (int x, int y)
  {
    return m_data[y][x] * (long)m_img.maxval / m_maxval;
  }
  void inline render_pixel (coord_t x, coord_t y, int *r, int *g, int *b)
  {
    coord_t xx, yy;
    m_scr_to_img.to_img (x, y, &xx, &yy);
    render_pixel_img (xx, yy, r, g, b);
  }
private:
  bool m_color;
};

typedef luminosity_t __attribute__ ((vector_size (sizeof (luminosity_t)*4))) vec_luminosity_t;

/* Cubic interpolation helper.  */

static inline luminosity_t
cubic_interpolate (luminosity_t p0, luminosity_t p1, luminosity_t p2, luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * x * (p2 - p0 +
			 x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}
static inline vec_luminosity_t
vec_cubic_interpolate (vec_luminosity_t p0, vec_luminosity_t p1, vec_luminosity_t p2, vec_luminosity_t p3, coord_t x)
{
  return p1 + (luminosity_t)0.5 * x * (p2 - p0 +
			 x * ((luminosity_t)2.0 * p0 - (luminosity_t)5.0 * p1 + (luminosity_t)4.0 * p2 - p3 +
			      x * ((luminosity_t)3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

inline luminosity_t
render::get_data (int x, int y)
{
  return m_lookup_table [m_data[y][x]];
}

/* Get same for rgb data.  */

inline luminosity_t
render::get_data_red (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].r];
}

inline luminosity_t
render::get_data_green (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].g];
}

inline luminosity_t
render::get_data_blue (int x, int y)
{
  return m_rgb_lookup_table [m_img.rgbdata[y][x].b];
}

#if 0
static inline luminosity_t
cap_color (luminosity_t val, luminosity_t weight, luminosity_t *diff, luminosity_t *cnt_neg, luminosity_t *cnt_pos)
{
  if (isnan (val))
    return 1;
  if (val < 0)
    {
      *cnt_neg += weight;
      *diff += val * weight;
      val = 0;
    }
  if (val > 1)
    {
      *cnt_pos += weight;
      *diff += (val - 1) * weight;
      val = 1;
    }
  return val;
}
#endif

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval.  */

inline void
render::set_color (luminosity_t r, luminosity_t g, luminosity_t b, int *rr, int *gg, int *bb)
{
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  /* Fancy capping seems to look weird.  */
#if 0
  luminosity_t r2 =r, g2= g, b2 = b;
  luminosity_t r1 =r, g1= g, b1 = b;
  luminosity_t diff = 0;
  luminosity_t cnt_neg = 0;
  luminosity_t cnt_pos = 0;
  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
  if (fabs (diff) > 0.0001)
    {
      luminosity_t lum = r * rwght + g * gwght + b * bwght;
      if (lum + diff < 0.00001)
	r = g = b = 0;
      else if (lum + diff > 0.99999)
	r = g = b = 1;
      else while (fabs (diff) > 0.0001)
	{
	  if (diff > 0)
	    {
	      luminosity_t add = diff / (3 - cnt_pos);
	      if (r < 1)
		r += add;
	      if (g < 1)
		g += add;
	      if (b < 1)
		b += add;
	    }
	  if (diff < 0)
	    {
	      luminosity_t add = diff / (3 - cnt_neg);
	      if (r > 0)
		r += add;
	      if (g > 0)
		g += add;
	      if (b > 0)
		b += add;
	    }
	  diff = 0;
	  cnt_neg = 0;
	  cnt_pos = 0;
	  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
	  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
	  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
	}
    }
#endif
  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval
   combining color and luminosity information.  */

inline void
render::set_color_luminosity (luminosity_t r, luminosity_t g, luminosity_t b, luminosity_t l, int *rr, int *gg, int *bb)
{
  luminosity_t r1, g1, b1;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  m_color_matrix.apply_to_rgb (l, l, l, &r1, &g1, &b1);
  l = r1 * rwght + g1 * gwght + b1 * bwght;
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));
  l = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, l));
  luminosity_t gr = (r * rwght + g * gwght + b * bwght);
  if (gr <= 0.00001 || l <= 0.00001)
    r = g = b = l;
  else
    {
      gr = l / gr;
      r *= gr;
      g *= gr;
      b *= gr;
    }
  r = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, r));
  g = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, g));
  b = std::min ((luminosity_t)1.0, std::max ((luminosity_t)0.0, b));

  *rr = m_out_lookup_table [(int)(r * (luminosity_t)65535.5)];
  *gg = m_out_lookup_table [(int)(g * (luminosity_t)65535.5)];
  *bb = m_out_lookup_table [(int)(b * (luminosity_t)65535.5)];
}

/* Determine grayscale value at a given position in the image.  */

inline luminosity_t
render::fast_get_img_pixel (int x, int y)
{
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}

/* Like modf but always round down.  */
static inline float
my_modf (float x, int *ptr)
{
  float f = floorf (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}
static inline double
my_modf (double x, int *ptr)
{
  float f = floorf (x);
  float ret = x - f;
  *ptr = f;
  return ret;
}

static inline long long
nearest_int (float x)
{
  return roundf (x);
}
static inline long long
nearest_int (double x)
{
  return round (x);
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline luminosity_t
render::get_img_pixel (coord_t xp, coord_t yp)
{
  luminosity_t val;

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  //int sx = xp, sy = yp;
  //luminosity_t rx = xp - sx, ry = yp - sy;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      vec_luminosity_t v1 = {get_data (sx-1, sy-1), get_data (sx, sy-1), get_data (sx+1, sy-1), get_data (sx+2, sy-1)};
      vec_luminosity_t v2 = {get_data (sx-1, sy-0), get_data (sx, sy-0), get_data (sx+1, sy-0), get_data (sx+2, sy-0)};
      vec_luminosity_t v3 = {get_data (sx-1, sy+1), get_data (sx, sy+1), get_data (sx+1, sy+1), get_data (sx+2, sy+1)};
      vec_luminosity_t v4 = {get_data (sx-1, sy+2), get_data (sx, sy+2), get_data (sx+1, sy+2), get_data (sx+2, sy+2)};
      vec_luminosity_t v = vec_cubic_interpolate (v1, v2, v3, v4, ry);
      val = cubic_interpolate (v[0], v[1], v[2], v[3], rx);
#if 0
  val = cubic_interpolate (cubic_interpolate (get_data ( sx-1, sy-1), get_data (sx-1, sy), get_data (sx-1, sy+1), get_data (sx-1, sy+2), ry),
			   cubic_interpolate (get_data ( sx-0, sy-1), get_data (sx-0, sy), get_data (sx-0, sy+1), get_data (sx-0, sy+2), ry),
			   cubic_interpolate (get_data ( sx+1, sy-1), get_data (sx+1, sy), get_data (sx+1, sy+1), get_data (sx+1, sy+2), ry),
			   cubic_interpolate (get_data ( sx+2, sy-1), get_data (sx+2, sy), get_data (sx+2, sy+1), get_data (sx+2, sy+2), ry),
			   rx);
#endif
      return val;
    }
    return 0;
  return val;
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline void
render::get_img_rgb_pixel (coord_t xp, coord_t yp, luminosity_t *r, luminosity_t *g, luminosity_t *b)
{
  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= (coord_t)0.5;
  yp -= (coord_t)0.5;
  int sx, sy;
  coord_t rx = my_modf (xp, &sx);
  coord_t ry = my_modf (yp, &sy);

  if (sx >= 1 && sx < m_img.width - 2 && sy >= 1 && sy < m_img.height - 2)
    {
      *r = cubic_interpolate (cubic_interpolate (get_data_red ( sx-1, sy-1), get_data_red (sx-1, sy), get_data_red (sx-1, sy+1), get_data_red (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx-0, sy-1), get_data_red (sx-0, sy), get_data_red (sx-0, sy+1), get_data_red (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx+1, sy-1), get_data_red (sx+1, sy), get_data_red (sx+1, sy+1), get_data_red (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_red ( sx+2, sy-1), get_data_red (sx+2, sy), get_data_red (sx+2, sy+1), get_data_red (sx+2, sy+2), ry),
			      rx);
      *g = cubic_interpolate (cubic_interpolate (get_data_green ( sx-1, sy-1), get_data_green (sx-1, sy), get_data_green (sx-1, sy+1), get_data_green (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx-0, sy-1), get_data_green (sx-0, sy), get_data_green (sx-0, sy+1), get_data_green (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx+1, sy-1), get_data_green (sx+1, sy), get_data_green (sx+1, sy+1), get_data_green (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_green ( sx+2, sy-1), get_data_green (sx+2, sy), get_data_green (sx+2, sy+1), get_data_green (sx+2, sy+2), ry),
			      rx);
      *b = cubic_interpolate (cubic_interpolate (get_data_blue ( sx-1, sy-1), get_data_blue (sx-1, sy), get_data_blue (sx-1, sy+1), get_data_blue (sx-1, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx-0, sy-1), get_data_blue (sx-0, sy), get_data_blue (sx-0, sy+1), get_data_blue (sx-0, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx+1, sy-1), get_data_blue (sx+1, sy), get_data_blue (sx+1, sy+1), get_data_blue (sx+1, sy+2), ry),
			      cubic_interpolate (get_data_blue ( sx+2, sy-1), get_data_blue (sx+2, sy), get_data_blue (sx+2, sy+1), get_data_blue (sx+2, sy+2), ry),
			      rx);
    }
  else
    {
      *r = 0;
      *g = 0;
      *b = 0;
      return;
    }
}

/* Sample square patch with center xc and yc and x1/y1, x2/y2 determining a coordinates
   of top left and top right corner.  */
luminosity_t
render::sample_img_square (coord_t xc, coord_t yc, coord_t x1, coord_t y1, coord_t x2, coord_t y2)
{
  luminosity_t acc = 0, weights = 0;
  int xmin = std::max ((int)(std::min (std::min (std::min (xc - x1, xc + x1), xc - x2), xc + x2) - 0.5), 0);
  int xmax = std::min ((int)ceil (std::max(std::max (std::max (xc - x1, xc + x1), xc - x2), xc + x2) + 0.5), m_img.width - 1);
  /* If the resolution is too small, just sample given point.  */
  if (xmax-xmin < 2)
    return get_img_pixel (xc, yc);
  /* For bigger resolution we can sample few points in the square.  */
  if (xmax-xmin < 6)
    {
      /* Maybe this will give more reproducible results, but it is very slow.  */
      int samples = (sqrt (x1 * x1 + y1 * y1) + 0.5) * 2;
      luminosity_t rec = 1.0 / samples;
      if (!samples)
	return get_img_pixel (xc, yc);
      for (int y = -samples ; y <= samples; y++)
	for (int x = -samples ; x <= samples; x++)
	  {
	    luminosity_t w = 1 + (samples - abs (x) - abs (y));
	    if (w < 0)
	      continue;
	    acc += w * get_img_pixel (xc + (x1 * x + x2 * y) * rec, yc + (y1 * x + y2 * y) * rec);
	    weights += w;
	  }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      int ymin = std::max ((int)(std::min (std::min (std::min (yc - y1, yc + y1), yc - y2), yc + y2) - 0.5), 0);
      int ymax = std::min ((int)ceil (std::max(std::max (std::max (yc - y1, yc + y1), yc - y2), yc + y2) + 0.5), m_img.height - 1);
      matrix2x2<coord_t> base (x1, x2,
			      y1, y2);
      matrix2x2<coord_t> inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
	{
	  for (int x = xmin ; x <= xmax; x++)
	    {
	      coord_t cx = x+0.5 -xc;
	      coord_t cy = y+0.5 -yc;
	      coord_t ccx, ccy;
	      inv.apply_to_vector (cx, cy, &ccx, &ccy);
	      luminosity_t w = fabs (ccx) + fabs (ccy);

	      //if (w < 1)
		//printf ("%.1f ",w);
	      //else
		//printf ("    ",w);
	      if (w < 1)
		{
		  w = (1 - w);
		  acc += w * get_data (x, y);
		  weights += w;
		}
	    }
	    //printf ("\n");
	 }
    }
  if (weights)
    return acc / weights;
  return 0;
}

/* Sample diagonal square.
   Square is specified by its center and size of diagonal.  */
luminosity_t
render::sample_scr_diag_square (coord_t xc, coord_t yc, coord_t diagonal_size)
{
  coord_t xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc + diagonal_size / 2, yc, &x1, &y1);
  m_scr_to_img.to_img (xc, yc + diagonal_size / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Sample diagonal square.
   Square is specified by center and width/height  */
luminosity_t
render::sample_scr_square (coord_t xc, coord_t yc, coord_t width, coord_t height)
{
  coord_t xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc - width / 2, yc + height / 2, &x1, &y1);
  m_scr_to_img.to_img (xc + width / 2, yc + height / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
inline luminosity_t
render::get_img_pixel_scr (coord_t x, coord_t y)
{
  coord_t xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_img_pixel (xp, yp);
}
#endif
