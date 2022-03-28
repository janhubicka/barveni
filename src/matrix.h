#ifndef MATRIX_H
#define MATRIX_H
/* Basic template for square matrix.  */
#include <cstring>
#include <stdio.h>

/* Square matrix template.  */
template <size_t dim>
class matrix
{
public:
  static const int m_dim = dim;
  double m_elements[m_dim][m_dim];

  matrix()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=(double) (i==j);
  }

  void
  randomize ()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=rand()%100;
  }

  matrix(const matrix &m)
  {
    memcpy(m_elements,m.m_elements,sizeof (m_elements));
  }

  matrix<dim> operator+ (const matrix<dim>& rhs) const
  {
    matrix<dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	ret.m_elements[j][i] = m_elements[j][i] + rhs.m_elements[j][i];
    return ret;
  }

  matrix<dim> operator* (const matrix<dim>& rhs) const
  {
    matrix<dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	{
	  double a = 0;
	  for (int k = 0; k < m_dim; k++)
	    a += m_elements[j][k] * rhs.m_elements[k][i];
	  ret.m_elements[j][i] = a;
	}
    return ret;
  }

  void
  print (FILE *f)
  {
    for (int j = 0; j < m_dim; j++)
      {
        for (int i = 0; i < m_dim; i++)
	  fprintf (f, " %f", m_elements[i][j]);
	fprintf (f, "\n");
      }
  }
};

/* 2x2 matrix with inverse opration.  */
class matrix2x2 : public matrix<2>
{
public:
  matrix2x2 (double m00, double m01, double m10, double m11)
  {
    m_elements[0][0]=m00;
    m_elements[0][1]=m01;
    m_elements[1][0]=m10;
    m_elements[1][1]=m11;
  }

  matrix2x2 ()
  {
  }

  inline matrix2x2
  invert ()
  {
    double a = m_elements[0][0];
    double b = m_elements[0][1];
    double c = m_elements[1][0];
    double d = m_elements[1][1];
    double det_rec = 1 / (a * d - b * c);
    matrix2x2 ret (d * det_rec, -b * det_rec, -c * det_rec, a * det_rec);
    return ret;
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
class matrix4x4 : public matrix<4>
{
public:
  matrix4x4 () { }

  matrix4x4 (double e00, double e01, double e02, double e03,
	     double e10, double e11, double e12, double e13,
	     double e20, double e21, double e22, double e23,
	     double e30, double e31, double e32, double e33)
  {
    m_elements[0][0]=e00; m_elements[1][0]=e01; m_elements[2][0]=e02; m_elements[3][0]=e03;
    m_elements[0][1]=e10; m_elements[1][1]=e11; m_elements[2][1]=e12; m_elements[3][1]=e03;
    m_elements[0][2]=e20; m_elements[1][2]=e21; m_elements[2][2]=e22; m_elements[3][2]=e03;
    m_elements[0][3]=e30; m_elements[1][3]=e31; m_elements[2][3]=e32; m_elements[3][3]=e03;
  }


  matrix4x4& operator=(const matrix<4>rhs)
  {
    memcpy(m_elements,rhs.m_elements,sizeof (m_elements));
    return *this;
  }

  inline void
  perspective_transform (double x, double y, double &xr, double &yr)
  {
    xr = (x * m_elements[0][0] + y * m_elements[0][1] + m_elements[0][2] + m_elements[0][3])
	 / (x * m_elements[2][0] + y * m_elements[2][1] + m_elements[2][2] + m_elements[2][3]);
    yr = (x * m_elements[1][0] + y * m_elements[1][1] + m_elements[1][2] + m_elements[1][3])
	 / (x * m_elements[3][0] + y * m_elements[3][1] + m_elements[3][2] + m_elements[3][3]);
  }

  inline void
  inverse_perspective_transform (double x, double y, double &xr, double &yr)
  {
    matrix2x2 m (m_elements[0][0] - m_elements[2][0] * x,
		 m_elements[0][1] - m_elements[2][1] * x,
		 m_elements[1][0] - m_elements[3][0] * y,
		 m_elements[1][1] - m_elements[3][1] * y);
    matrix2x2 m2 = m.invert ();
    double xx = (m_elements[2][2]+m_elements[2][3])*x - m_elements[0][2] - m_elements[0][3];
    double yy = (m_elements[3][2]+m_elements[3][3])*y - m_elements[1][2] - m_elements[1][3];

    xr = m2.m_elements[0][0] * xx + m2.m_elements[0][1] * yy;
    yr = m2.m_elements[1][0] * xx + m2.m_elements[1][1] * yy;
  }

  inline void
  apply_to_rgb (double r, double g, double b, double *rr, double *gg, double *bb)
  {
    *rr = r * m_elements[0][0] + g * m_elements[1][0] + b * m_elements[2][0] + m_elements[3][0];
    *gg = r * m_elements[0][1] + g * m_elements[1][1] + b * m_elements[2][1] + m_elements[3][1];
    *bb = r * m_elements[0][2] + g * m_elements[1][2] + b * m_elements[2][2] + m_elements[3][2];
  }
};

// http://www.graficaobscura.com/matrix/index.html
static const double rwght = 0.3086, gwght = 0.6094, bwght = 0.0820;

// http://www.graficaobscura.com/matrix/index.html
class saturation_matrix : public matrix4x4
{
public:
  saturation_matrix (double s)
  : matrix4x4 ((1-s)*rwght + s, (1-s)*gwght    , (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght + s, (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght    , (1-s)*bwght + s, 0,
	       0,             0,              0,                  0)
  {}
};
#endif
