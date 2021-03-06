#include <math.h>
#include <algorithm>
#include "include/scr-to-img.h"

/* Initilalize the translation matrix to PARAM.  */
void
scr_to_img::set_parameters (scr_to_img_parameters param)
{
  m_param = param;
  trans_matrix m;

  if (param.tilt_x_x!= 0)
    {
      rotation_matrix rotation (param.tilt_x_x, 0, 2);
      m = rotation * m;
    }
  if (param.tilt_x_y!= 0)
    {
      rotation_matrix rotation (param.tilt_x_y, 1, 2);
      m = rotation * m;
    }
  if (param.tilt_y_x!= 0)
    {
      rotation_matrix rotation (param.tilt_y_x, 0, 3);
      m = rotation * m;
    }
  if (param.tilt_y_y!= 0)
    {
      rotation_matrix rotation (param.tilt_y_y, 1, 3);
      m = rotation * m;
    }
  coord_t c1x, c1y;
  coord_t c2x, c2y;
  m.inverse_perspective_transform (param.coordinate1_x, param.coordinate1_y, c1x, c1y);
  m.inverse_perspective_transform (param.coordinate2_x, param.coordinate2_y, c2x, c2y);

  /* Change-of-basis matrix.  */
  change_of_basis_matrix basis (c1x, c1y, c2x, c2y);
  m_matrix = basis * m;
}

/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (coord_t x1, coord_t y1,
		       coord_t x2, coord_t y2,
		       int *scr_xshift, int *scr_yshift,
		       int *scr_width, int *scr_height)
{
  /* Compute all the corners.  */
  coord_t xul,xur,xdl,xdr;
  coord_t yul,yur,ydl,ydr;

  to_scr (x1, y1, &xul, &yul);
  to_scr (x2, y1, &xur, &yur);
  to_scr (x1, y2, &xdl, &ydl);
  to_scr (x2, y2, &xdr, &ydr);

  /* Find extremas.  */
  coord_t minx = std::min (std::min (std::min (xul, xur), xdl), xdr);
  coord_t miny = std::min (std::min (std::min (yul, yur), ydl), ydr);
  coord_t maxx = std::max (std::max (std::max (xul, xur), xdl), xdr);
  coord_t maxy = std::max (std::max (std::max (yul, yur), ydl), ydr);

  /* Determine the coordinates.  */
  *scr_xshift = -minx - 1;
  *scr_yshift = -miny - 1;
  *scr_width = maxx-minx + 2;
  *scr_height = maxy-miny + 2;
}
/* Determine rectangular section of the screen to which the whole image
   with dimension img_width x img_height fits.

   The section is having dimensions scr_width x scr_height and will
   start at position (-scr_xshift, -scr_yshift).  */
void
scr_to_img::get_range (int img_width, int img_height,
		       int *scr_xshift, int *scr_yshift,
		       int *scr_width, int *scr_height)
{
  get_range (0.0, 0.0, (coord_t)img_width, (coord_t)img_height,
	     scr_xshift, scr_yshift,
	     scr_width, scr_height);
}
