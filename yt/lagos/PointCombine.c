/************************************************************************
* Copyright (C) 2007 Matthew Turk.  All Rights Reserved.
*
* This file is part of yt.
*
* yt is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
************************************************************************/


//
// PointCombine
//   A module for merging points from different grids, in various ways.
//   Used for projections, interpolations, and binning profiles.
//

#include "Python.h"

#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <ctype.h>

#include "numpy/ndarrayobject.h"

#define max(A,B) ((A) > (B) ? (A) : (B))
#define min(A,B) ((A) < (B) ? (A) : (B))

static PyObject *_combineGridsError;

static PyObject *
Py_CombineGrids(PyObject *obj, PyObject *args)
{
    PyObject    *ogrid_src_x, *ogrid_src_y, *ogrid_src_vals,
        *ogrid_src_mask, *ogrid_src_wgt;
    PyObject    *ogrid_dst_x, *ogrid_dst_y, *ogrid_dst_vals,
        *ogrid_dst_mask, *ogrid_dst_wgt;

    PyArrayObject    *grid_src_x, *grid_src_y, **grid_src_vals,
            *grid_src_mask, *grid_src_wgt;
    PyArrayObject    *grid_dst_x, *grid_dst_y, **grid_dst_vals,
            *grid_dst_mask, *grid_dst_wgt;

    int NumArrays, src_len, dst_len, refinement_factor;

    if (!PyArg_ParseTuple(args, "OOOOOOOOOOi",
            &ogrid_src_x, &ogrid_src_y, 
        &ogrid_src_mask, &ogrid_src_wgt, &ogrid_src_vals,
            &ogrid_dst_x, &ogrid_dst_y,
        &ogrid_dst_mask, &ogrid_dst_wgt, &ogrid_dst_vals,
        &refinement_factor))
    return PyErr_Format(_combineGridsError,
            "CombineGrids: Invalid parameters.");

    /* First the regular source arrays */

    grid_src_x    = (PyArrayObject *) PyArray_FromAny(ogrid_src_x,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    src_len = PyArray_SIZE(grid_src_x);

    grid_src_y    = (PyArrayObject *) PyArray_FromAny(ogrid_src_y,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_src_y) != src_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: src_x and src_y must be the same shape.");
    goto _fail;
    }

    grid_src_mask = (PyArrayObject *) PyArray_FromAny(ogrid_src_mask,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_src_mask) != src_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: src_x and src_mask must be the same shape.");
    goto _fail;
    }

    grid_src_wgt  = (PyArrayObject *) PyArray_FromAny(ogrid_src_wgt,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_src_wgt) != src_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: src_x and src_wgt must be the same shape.");
    goto _fail;
    }

    grid_dst_x    = (PyArrayObject *) PyArray_FromAny(ogrid_dst_x,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    dst_len = PyArray_SIZE(grid_dst_x);

    grid_dst_y    = (PyArrayObject *) PyArray_FromAny(ogrid_dst_y,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_dst_y) != dst_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: dst_x and dst_y must be the same shape.");
    goto _fail;
    }

    grid_dst_mask = (PyArrayObject *) PyArray_FromAny(ogrid_dst_mask,
                    PyArray_DescrFromType(NPY_INT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_dst_mask) != dst_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: dst_x and dst_mask must be the same shape.");
    goto _fail;
    }

    grid_dst_wgt  = (PyArrayObject *) PyArray_FromAny(ogrid_dst_wgt,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 0,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(PyArray_SIZE(grid_dst_wgt) != dst_len) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: dst_x and dst_wgt must be the same shape.");
    goto _fail;
    }

    /* Now we do our lists of values */
    NumArrays = PySequence_Length(ogrid_src_vals);
    if (NumArrays < 1) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: You have to pass me lists of things.");
    goto _fail;
    }
    if (!(PySequence_Length(ogrid_dst_vals) == NumArrays)) {
    PyErr_Format(_combineGridsError,
             "CombineGrids: Sorry, but your lists of values are different lengths.");
    goto _fail;
    }

    grid_src_vals = malloc(NumArrays * sizeof(PyArrayObject*));
    grid_dst_vals = malloc(NumArrays * sizeof(PyArrayObject*));
    npy_float64 **src_vals = malloc(NumArrays * sizeof(npy_float64*));
    npy_float64 **dst_vals = malloc(NumArrays * sizeof(npy_float64*));
    PyObject *temp_object;
    int i;
    for (i = 0; i < NumArrays; i++) {
      temp_object = PySequence_GetItem(ogrid_src_vals, i);
      grid_src_vals[i] = (PyArrayObject *) PyArray_FromAny(
          temp_object,
          PyArray_DescrFromType(NPY_FLOAT64), 1, 0,
          NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
      src_vals[i] = (npy_float64 *) grid_src_vals[i]->data;
      Py_DECREF(temp_object);

      temp_object = PySequence_GetItem(ogrid_dst_vals, i);
      grid_dst_vals[i] = (PyArrayObject *) PyArray_FromAny(
          temp_object,
          PyArray_DescrFromType(NPY_FLOAT64), 1, 0,
          NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
      dst_vals[i] = (npy_float64 *) grid_dst_vals[i]->data;
      Py_DECREF(temp_object);
    }

    /* Now we're all set to call our sub-function. */

    npy_int64     *src_x    = (npy_int64 *) grid_src_x->data;
    npy_int64     *src_y    = (npy_int64 *) grid_src_y->data;
    npy_float64 *src_wgt  = (npy_float64 *) grid_src_wgt->data;
    npy_int64     *src_mask = (npy_int64 *) grid_src_mask->data;

    npy_int64     *dst_x    = (npy_int64 *) grid_dst_x->data;
    npy_int64     *dst_y    = (npy_int64 *) grid_dst_y->data;
    npy_float64 *dst_wgt  = (npy_float64 *) grid_dst_wgt->data;
    npy_int64     *dst_mask = (npy_int64 *) grid_dst_mask->data;

    int si, di, x_off, y_off;
    npy_int64  fine_x, fine_y, init_x, init_y;
    int num_found = 0;

    for (si = 0; si < src_len; si++) {
      if (src_x[si] < 0) continue;
      init_x = refinement_factor * src_x[si];
      init_y = refinement_factor * src_y[si];
      for (x_off = 0; x_off < refinement_factor; x_off++) {
        for(y_off = 0; y_off < refinement_factor; y_off++) {
          fine_x = init_x + x_off;
          fine_y = init_y + y_off;
          for (di = 0; di < dst_len; di++) {
            if (dst_x[di] < 0) continue;
            if ((fine_x == dst_x[di]) &&
                (fine_y == dst_y[di])) {
              num_found++;
              dst_wgt[di] += src_wgt[di];
              dst_mask[di] = ((src_mask[si] && dst_mask[di]) ||
                  ((refinement_factor != 1) && (dst_mask[di])));
              // So if they are on the same level, then take the logical and
              // otherwise, set it to the destination mask
              src_x[si] = -1;
              for (i = 0; i < NumArrays; i++) {
                dst_vals[i][di] += src_vals[i][si];
              }
              if (refinement_factor == 1) break;
            }
          }
        }
      }
    }

    Py_DECREF(grid_src_x);
    Py_DECREF(grid_src_y);
    Py_DECREF(grid_src_mask);
    Py_DECREF(grid_src_wgt);

    Py_DECREF(grid_dst_x);
    Py_DECREF(grid_dst_y);
    Py_DECREF(grid_dst_mask);
    Py_DECREF(grid_dst_wgt);

    if (NumArrays > 0){
      for (i = 0; i < NumArrays; i++) {
        Py_DECREF(grid_src_vals[i]);
        Py_DECREF(grid_dst_vals[i]);
      }
    }

    free(grid_src_vals);
    free(grid_dst_vals);
    free(src_vals);
    free(dst_vals);

    PyObject *onum_found = PyInt_FromLong((long)num_found);
    return onum_found;

_fail:
    Py_XDECREF(grid_src_x);
    Py_XDECREF(grid_src_y);
    Py_XDECREF(grid_src_wgt);
    Py_XDECREF(grid_src_mask);

    Py_XDECREF(grid_dst_x);
    Py_XDECREF(grid_dst_y);
    Py_XDECREF(grid_dst_wgt);
    Py_XDECREF(grid_dst_mask);
    if (NumArrays > 0){
      for (i = 0; i < NumArrays; i++) {
        Py_XDECREF(grid_src_vals[i]);
        Py_XDECREF(grid_dst_vals[i]);
      }
    }
    return NULL;

}

/* These functions are both called with
    func(cubedata, griddata) */
static void dcRefine(npy_float64 *val1, npy_float64 *val2) {
    *val1 = *val2;
}

static void dcReplace(npy_float64 *val1, npy_float64 *val2) {
    *val2 = *val1;
}

static PyObject *_profile2DError;

static PyObject *Py_Bin2DProfile(PyObject *obj, PyObject *args)
{
    int i, j;
    PyObject *obins_x, *obins_y, *owsource, *obsource, *owresult, *obresult, *oused;
    PyArrayObject *bins_x, *bins_y, *wsource, *bsource, *wresult, *bresult, *used;

    if (!PyArg_ParseTuple(args, "OOOOOOO",
                &obins_x, &obins_y, &owsource, &obsource,
                &owresult, &obresult, &oused))
        return PyErr_Format(_profile2DError,
                "Bin2DProfile: Invalid parameters.");
    i = 0;

    bins_x = (PyArrayObject *) PyArray_FromAny(obins_x,
                    PyArray_DescrFromType(NPY_INT64), 1, 1,
                    NPY_IN_ARRAY, NULL);
    if(bins_x==NULL) {
    PyErr_Format(_profile2DError,
             "Bin2DProfile: One dimension required for bins_x.");
    goto _fail;
    }
    
    bins_y = (PyArrayObject *) PyArray_FromAny(obins_y,
                    PyArray_DescrFromType(NPY_INT64), 1, 1,
                    NPY_IN_ARRAY, NULL);
    if((bins_y==NULL) || (PyArray_SIZE(bins_x) != PyArray_SIZE(bins_y))) {
    PyErr_Format(_profile2DError,
             "Bin2DProfile: One dimension required for bins_y, same size as bins_x.");
    goto _fail;
    }
    
    wsource = (PyArrayObject *) PyArray_FromAny(owsource,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_IN_ARRAY, NULL);
    if((wsource==NULL) || (PyArray_SIZE(bins_x) != PyArray_SIZE(wsource))) {
    PyErr_Format(_profile2DError,
             "Bin2DProfile: One dimension required for wsource, same size as bins_x.");
    goto _fail;
    }
    
    bsource = (PyArrayObject *) PyArray_FromAny(obsource,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_IN_ARRAY, NULL);
    if((bsource==NULL) || (PyArray_SIZE(bins_x) != PyArray_SIZE(bsource))) {
    PyErr_Format(_profile2DError,
             "Bin2DProfile: One dimension required for bsource, same size as bins_x.");
    goto _fail;
    }

    wresult = (PyArrayObject *) PyArray_FromAny(owresult,
                    PyArray_DescrFromType(NPY_FLOAT64), 2,2,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if(wresult==NULL){
    PyErr_Format(_profile2DError,
             "Bin2DProfile: Two dimensions required for wresult.");
    goto _fail;
    }

    bresult = (PyArrayObject *) PyArray_FromAny(obresult,
                    PyArray_DescrFromType(NPY_FLOAT64), 2,2,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((bresult==NULL) ||(PyArray_SIZE(wresult) != PyArray_SIZE(bresult))
       || (PyArray_DIM(bresult,0) != PyArray_DIM(wresult,0))){
    PyErr_Format(_profile2DError,
             "Bin2DProfile: Two dimensions required for bresult, same shape as wresult.");
    goto _fail;
    }
    
    used = (PyArrayObject *) PyArray_FromAny(oused,
                    PyArray_DescrFromType(NPY_FLOAT64), 2,2,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((used==NULL) ||(PyArray_SIZE(used) != PyArray_SIZE(wresult))
       || (PyArray_DIM(used,0) != PyArray_DIM(wresult,0))){
    PyErr_Format(_profile2DError,
             "Bin2DProfile: Two dimensions required for used, same shape as wresult.");
    goto _fail;
    }

    npy_float64 wv;
    int n;

    for(n=0; n<bins_x->dimensions[0]; n++) {
      i = bins_x->data[n];
      j = bins_y->data[n];
      *(npy_float64*)PyArray_GETPTR2(wresult, i, j) += 
        wsource->data[n];
      *(npy_float64*)PyArray_GETPTR2(bresult, i, j) += 
        wsource->data[n] * bsource->data[n];
      *(npy_float64*)PyArray_GETPTR2(used, i, j) = 1.0;
    }

      Py_DECREF(bins_x); 
      Py_DECREF(bins_y); 
      Py_DECREF(wsource); 
      Py_DECREF(bsource); 
      Py_DECREF(wresult); 
      Py_DECREF(bresult); 
      Py_DECREF(used);
    
      PyObject *onum_found = PyInt_FromLong((long)1);
      return onum_found;
    
    _fail:
      Py_XDECREF(bins_x); 
      Py_XDECREF(bins_y); 
      Py_XDECREF(wsource); 
      Py_XDECREF(bsource); 
      Py_XDECREF(wresult); 
      Py_XDECREF(bresult); 
      Py_XDECREF(used);
      return NULL;

}

static PyObject *_dataCubeError;

static PyObject *DataCubeGeneric(PyObject *obj, PyObject *args,
                             void (*to_call)(npy_float64*,npy_float64*))
{
    /* Standard boilerplate unpacking...  */

    /* 
       rf              (py_int)                 i
       grid_leftedge   (npy_float64 COERCE)     O
       dx_grid         (npy_float64 COERCE)     O
       griddata        (npy_float64 array)      O
       childmask       (npy_bool array)         O
       cube_leftedge   (npy_float64 COERCE)     O
       cube_rightedge  (npy_float64 COERCE)     O
       dx_cube         (npy_float64 COERCE)     O
       cubedata        (npy_float64 array)      O
       lastlevel       (py_int)                 i
    */


    int ll;

    PyObject *og_le, *og_dx, *og_data, *og_cm,
             *oc_le, *oc_re, *oc_dx, *oc_data;
    PyArrayObject *g_le, *g_dx, *g_data, *g_cm,
                  *c_le, *c_re, *c_dx, *c_data;
    npy_float64 *ag_le, *ag_dx, *ag_data, 
                *ac_le, *ac_re, *ac_dx, *ac_data;
    npy_int *ag_cm;

    if (!PyArg_ParseTuple(args, "OOOOOOOOi",
            &og_le, &og_dx, &og_data, &og_cm,
            &oc_le, &oc_re, &oc_dx, &oc_data,
        &ll))
    return PyErr_Format(_dataCubeError,
            "DataCubeGeneric: Invalid parameters.");

    /* First the regular source arrays */

    g_le    = (PyArrayObject *) PyArray_FromAny(og_le,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((g_le==NULL) || (PyArray_SIZE(g_le) != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three values, one dimension required for g_le.");
    goto _fail;
    }
    ag_le = (npy_float64*) g_le->data;

    g_dx    = (PyArrayObject *) PyArray_FromAny(og_dx,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((g_dx==NULL) || (PyArray_SIZE(g_dx) != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three values, one dimension required for g_dx.");
    goto _fail;
    }
    ag_dx = (npy_float64*) g_dx->data;

    g_data    = (PyArrayObject *) PyArray_FromAny(og_data,
                    PyArray_DescrFromType(NPY_FLOAT64), 3, 3,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((g_data==NULL) || (g_data->nd != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three dimensions required for g_data.");
    goto _fail;
    }
    ag_data = (npy_float64*) g_data->data;

    g_cm    = (PyArrayObject *) PyArray_FromAny(og_cm,
                    PyArray_DescrFromType(NPY_INT), 3, 3,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((g_cm==NULL) || (g_cm->nd != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three dimensions required for g_cm.");
    goto _fail;
    }
    ag_cm = (npy_int*) g_cm->data;

    /* Now the cube */
 
    c_le    = (PyArrayObject *) PyArray_FromAny(oc_le,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((c_le==NULL) || (PyArray_SIZE(c_le) != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three values, one dimension required for c_le.");
    goto _fail;
    }
    ac_le = (npy_float64*) c_le->data;

    c_re    = (PyArrayObject *) PyArray_FromAny(oc_re,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((c_re==NULL) || (PyArray_SIZE(c_re) != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three values, one dimension required for c_re.");
    goto _fail;
    }
    ac_re = (npy_float64*) c_re->data;

    c_dx    = (PyArrayObject *) PyArray_FromAny(oc_dx,
                    PyArray_DescrFromType(NPY_FLOAT64), 1, 1,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((c_dx==NULL) || (PyArray_SIZE(c_dx) != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three values, one dimension required for c_dx.");
    goto _fail;
    }
    ac_dx = (npy_float64*) c_dx->data;

    c_data    = (PyArrayObject *) PyArray_FromAny(oc_data,
                    PyArray_DescrFromType(NPY_FLOAT64), 3, 3,
                    NPY_INOUT_ARRAY | NPY_UPDATEIFCOPY, NULL);
    if((c_data==NULL) || (c_data->nd != 3)) {
    PyErr_Format(_dataCubeError,
             "CombineGrids: Three dimensions required for c_data.");
    goto _fail;
    }
    ac_data = (npy_float64*) c_data->data;

    /* And let's begin */

    npy_int64 xg, yg, zg, xc, yc, zc, cmax_x, cmax_y, cmax_z,
              cmin_x, cmin_y, cmin_z, cm;
    npy_float64 *val1, *val2;
    long int total=0;

    for (xg = 0; xg < g_data->dimensions[0]; xg++) {
      if (ag_le[0]+ag_dx[0]*xg     > ac_re[0]) continue;
      if (ag_le[0]+ag_dx[0]*(xg+1) < ac_le[0]) continue;
      cmin_x = max(floorl((ag_le[0]+ag_dx[0]*xg     - ac_le[0])/ac_dx[0]),0);
      cmax_x = min( ceill((ag_le[0]+ag_dx[0]*(xg+1) - ac_le[0])/ac_dx[0]),c_data->dimensions[0]);
      for (yg = 0; yg < g_data->dimensions[1]; yg++) {
        if (ag_le[1]+ag_dx[1]*yg     > ac_re[1]) continue;
        if (ag_le[1]+ag_dx[1]*(yg+1) < ac_le[1]) continue;
        cmin_y = max(floorl((ag_le[1]+ag_dx[1]*yg     - ac_le[1])/ac_dx[1]),0);
        cmax_y = min( ceill((ag_le[1]+ag_dx[1]*(yg+1) - ac_le[1])/ac_dx[1]),c_data->dimensions[1]);
        for (zg = 0; zg < g_data->dimensions[2]; zg++) {
          cm = *(npy_int *)PyArray_GETPTR3(g_cm,xg,yg,zg);
          if ((!ll) && (cm == 0)) continue;
          if (ag_le[2]+ag_dx[2]*zg     > ac_re[2]) continue;
          if (ag_le[2]+ag_dx[2]*(zg+1) < ac_le[2]) continue;
          cmin_z = max(floorl((ag_le[2]+ag_dx[2]*zg     - ac_le[2])/ac_dx[2]),0);
          cmax_z = min( ceill((ag_le[2]+ag_dx[2]*(zg+1) - ac_le[2])/ac_dx[2]),c_data->dimensions[2]);
          for (xc = cmin_x; xc < cmax_x ; xc++) {
            for (yc = cmin_y; yc < cmax_y ; yc++) {
              for (zc = cmin_z; zc < cmax_z ; zc++) {
                val1 = PyArray_GETPTR3(c_data,xc,yc,zc);
                val2 = PyArray_GETPTR3(g_data,xg,yg,zg);
                to_call(val1, val2);
                total += 1;
              }
            }
          }
        }
      }
    }

    /* Cleanup time */

    Py_DECREF(g_le);
    Py_DECREF(g_dx);
    Py_DECREF(g_data);
    Py_DECREF(g_cm);
    Py_DECREF(c_le);
    Py_DECREF(c_re);
    Py_DECREF(c_dx);
    Py_DECREF(c_data);

    PyObject *status = PyInt_FromLong(total);
    return status;
    
_fail:
    Py_XDECREF(g_le);
    Py_XDECREF(g_dx);
    Py_XDECREF(g_data);
    Py_XDECREF(g_cm);
    Py_XDECREF(c_le);
    Py_XDECREF(c_re);
    Py_XDECREF(c_dx);
    Py_XDECREF(c_data);
    return NULL;

}

static PyObject *
Py_DataCubeRefine(PyObject *obj, PyObject *args)
{
    PyObject* to_return = DataCubeGeneric(obj, args, dcRefine);
    return to_return;
}

static PyObject *
Py_DataCubeReplace(PyObject *obj, PyObject *args)
{
    PyObject* to_return = DataCubeGeneric(obj, args, dcReplace);
    return to_return;
}



static PyObject *_interpolateError;

static void
Interpolate(long num_axis_points, npy_float64 *axis, PyArrayObject* table,
            PyArrayObject *desiredvals, long num_columns, npy_int32 *columns,
            PyArrayObject *outputvals)
{
    //int table_rows = table->dimensions[0];
    int num_desireds = desiredvals->dimensions[0];

    npy_int axis_ind, col_ind;
    npy_int32 column;
    npy_int64 desired_num;

    npy_float64 desired;

    npy_float64 logtem0 = log10(axis[0]);
    npy_float64 logtem9 = log10(axis[num_axis_points-1]);
    npy_float64 dlogtem = (logtem9-logtem0)/(num_axis_points-1);
    npy_float64 t1, t2, tdef, ki, kip;
    npy_float64 *t;

    for (desired_num = 0 ; desired_num < num_desireds ; desired_num++) {
        t = (npy_float64*)PyArray_GETPTR1(desiredvals, desired_num);
        desired = log10l(*t);
        axis_ind = min(num_axis_points-1,
                   max(0,(int)((desired-logtem0)/dlogtem)+1));
        t1 = (logtem0 + (axis_ind-1)*dlogtem);
        t2 = (logtem0 + (axis_ind+0)*dlogtem);
        tdef = t2 - t1;
        for (column = 0 ; column < num_columns ; column++) {
            col_ind = (npy_int) columns[column];
            ki  = *(npy_float64*)PyArray_GETPTR2(table, (npy_int) (axis_ind-1), col_ind);
            kip = *(npy_float64*)PyArray_GETPTR2(table, (npy_int) (axis_ind+0), col_ind);
            *(npy_float64*) PyArray_GETPTR2(outputvals, desired_num, column) =
                    ki+(desired-t1)*(kip-ki)/tdef;
        }
    }
    return;
}

static PyObject *
Py_Interpolate(PyObject *obj, PyObject *args)
{
    PyObject   *oaxis, *otable, *odesired, *ooutputvals, *ocolumns;
    PyArrayObject   *axis, *table, *desired, *outputvals, *columns;

    if (!PyArg_ParseTuple(args, "OOOOO",
        &oaxis, &otable, &odesired, &ooutputvals, &ocolumns))
        return PyErr_Format(_interpolateError,
                    "Interpolate: Invalid parameters.");

    /* Align, Byteswap, Contiguous, Typeconvert */
    axis          =  (PyArrayObject *) PyArray_FromAny(oaxis         , PyArray_DescrFromType(NPY_FLOAT64), 1, 0, NPY_ENSURECOPY | NPY_UPDATEIFCOPY, NULL );
    table         =  (PyArrayObject *) PyArray_FromAny(otable        , PyArray_DescrFromType(NPY_FLOAT64), 1, 0, NPY_ENSURECOPY | NPY_UPDATEIFCOPY, NULL );
    desired       =  (PyArrayObject *) PyArray_FromAny(odesired      , PyArray_DescrFromType(NPY_FLOAT64), 1, 0, NPY_ENSURECOPY | NPY_UPDATEIFCOPY, NULL );
    outputvals    =  (PyArrayObject *) PyArray_FromAny(ooutputvals   , PyArray_DescrFromType(NPY_FLOAT64), 1, 0, NPY_ENSURECOPY | NPY_UPDATEIFCOPY, NULL );
    columns       =  (PyArrayObject *) PyArray_FromAny(ocolumns      ,   PyArray_DescrFromType(NPY_INT32), 1, 0, NPY_ENSURECOPY | NPY_UPDATEIFCOPY, NULL );

    if (!axis || !table || !desired || !outputvals || !columns) {
        PyErr_Format(_interpolateError,
                  "Interpolate: error converting array inputs.");
        goto _fail;
    }

    if (columns->dimensions[0] != outputvals->dimensions[1]) {
        PyErr_Format(_interpolateError,
                 "Interpolate: number of columns requested must match number "
                 "of columns in output buffer. %i", (int) columns->dimensions[0]);
        goto _fail;
    }

    Interpolate(axis->dimensions[0],
              (npy_float64 *) axis->data,
              table, desired,
              columns->dimensions[0],
              (npy_int32 *) columns->data,
              outputvals);
    Py_XDECREF(axis);
    Py_XDECREF(table);
    Py_XDECREF(desired);
    Py_XDECREF(outputvals);
    Py_XDECREF(columns);

    /* Align, Byteswap, Contiguous, Typeconvert */
    return Py_None;

  _fail:
    Py_XDECREF(axis);
    Py_XDECREF(table);
    Py_XDECREF(desired);
    Py_XDECREF(outputvals);
    Py_XDECREF(columns);


    return NULL;
}

static PyMethodDef _combineMethods[] = {
    {"CombineGrids", Py_CombineGrids, METH_VARARGS},
    {"Interpolate", Py_Interpolate, METH_VARARGS},
    {"DataCubeRefine", Py_DataCubeRefine, METH_VARARGS},
    {"DataCubeReplace", Py_DataCubeReplace, METH_VARARGS},
    {"Bin2DProfile", Py_Bin2DProfile, METH_VARARGS},
    {NULL, NULL} /* Sentinel */
};

/* platform independent*/
#ifdef MS_WIN32
__declspec(dllexport)
#endif

void initPointCombine(void)
{
    PyObject *m, *d;
    m = Py_InitModule("PointCombine", _combineMethods);
    d = PyModule_GetDict(m);
    _combineGridsError = PyErr_NewException("PointCombine.CombineGridsError", NULL, NULL);
    PyDict_SetItemString(d, "error", _combineGridsError);
    _interpolateError = PyErr_NewException("PointCombine.InterpolateError", NULL, NULL);
    PyDict_SetItemString(d, "error", _interpolateError);
    _dataCubeError = PyErr_NewException("PointCombine.DataCubeError", NULL, NULL);
    PyDict_SetItemString(d, "error", _dataCubeError);
    _profile2DError = PyErr_NewException("PointCombine.Profile2DError", NULL, NULL);
    PyDict_SetItemString(d, "error", _profile2DError);
    import_array();
}

/*
 * Local Variables:
 * mode: C
 * c-file-style: "python"
 * End:
 */
