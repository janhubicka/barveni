#ifndef RENDERSUPERPOSEIMG_H
#define RENDERSUPERPOSEIMG_H
#include "render.h"
#include "screen.h"
class render_superpose_img : public render
{
public:
  inline render_superpose_img (scr_to_img_parameters param, image_data &data, int dst_maxval, screen *screen)
   : render (param, data, dst_maxval),
     m_screen (screen), m_color (false) { }
  void inline render_pixel_img (double x, double y, int *r, int *g, int *b);
  void inline render_pixel_img_antialias (double x, double y, double pixelsize, int steps, int *r, int *g, int *b);
  /* If set, use color scan for input.  */
  void set_color_display () { if (m_img.rgbdata) m_color = 1; }
private:
  void inline sample_pixel_img (double x, double y, double scr_x, double scr_y, double *r, double *g, double *b);
  screen *m_screen;
  bool m_color;
};

flatten_attr inline void
render_superpose_img::sample_pixel_img (double x, double y, double scr_x, double scr_y, double *r, double *g, double *b)
{
  double gg, rr, bb;
  int ix, iy;

  ix = (long long) round (scr_x* screen::size) & (screen::size - 1);
  iy = (long long) round (scr_y* screen::size) & (screen::size - 1);
  if (!m_color)
    {
      double graydata = get_img_pixel (x, y);
      *r = graydata * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      *g = graydata * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      *b = graydata * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
  else
    {
      double rr, gg, bb;
      get_img_rgb_pixel (x, y, &rr, &gg, &bb);
      *r = rr * m_screen->mult[iy][ix][0] + m_screen->add[iy][ix][0];
      *g = gg * m_screen->mult[iy][ix][1] + m_screen->add[iy][ix][1];
      *b = bb * m_screen->mult[iy][ix][2] + m_screen->add[iy][ix][2];
    }
}
flatten_attr void
render_superpose_img::render_pixel_img (double x, double y, int *r, int *g, int *b)
{
  double rr, gg, bb;
  double scr_x, scr_y;
  m_scr_to_img.to_scr (x, y, &scr_x, &scr_y);
  render_superpose_img::sample_pixel_img (x, y, scr_x, scr_y, &rr, &gg, &bb);
  set_color (rr, gg, bb, r,g,b);
}
flatten_attr void
render_superpose_img::render_pixel_img_antialias (double x, double y, double pixelsize, int steps, int *r, int *g, int *b)
{
  double rr = 0, gg = 0, bb = 0;
  double scr_x, scr_y;

  if (pixelsize <= 1)
    {
      m_scr_to_img.to_scr (x, y, &scr_x, &scr_y);
      sample_pixel_img (x, y, scr_x, scr_y, &rr, &gg, &bb);
      set_color (rr, gg, bb, r,g,b);
      return;
    }
  else
    {
      x -= pixelsize/4;
      y -= pixelsize/4;
      pixelsize *= 2;
      int steps2 = (pixelsize + 0.5) * 2;
      if (steps2 < steps)
	steps = steps2;
    }
  for (int xx = 0; xx < steps; xx ++)
    for (int yy = 0; yy < steps; yy ++)
      {
        double rrr, ggg, bbb;
	double xp = x + xx * (pixelsize / steps);
	double yp = y + yy * (pixelsize / steps);
        m_scr_to_img.to_scr (xp, yp, &scr_x, &scr_y);
        sample_pixel_img (xp, yp, scr_x, scr_y, &rrr, &ggg, &bbb);
	rr += rrr;
	bb += bbb;
	gg += ggg;
      }
  set_color (3 * rr / (steps * steps), 3 * gg / (steps * steps), 3 * bb / (steps * steps), r,g,b);
}
#endif
