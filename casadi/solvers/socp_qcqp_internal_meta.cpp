/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


      #include "socp_qcqp_internal.hpp"
      #include <string>

      const std::string casadi::SOCPQCQPInternal::meta_doc=
      "\n"
"Solve a QCQP with an SocpSolver\n"
"\n"
"Note: this implementation relies on Cholesky decomposition: Chol(H) = L -> H = LL' with L lower triangular This requires Pi, H to be positive definite.\n"
"Positive semi-definite is not sufficient. Notably, H==0 will not work.\n"
"\n"
"A better implementation would rely on matrix square root, but we need\n"
"singular value decomposition to implement that.\n"
"\n"
"This implementation makes use of the epigraph reformulation:*  min f(x) *    x * *   min  t *    x, t  f(x) <= t *\n"
"\n"
"This implementation makes use of the following identity:*  || Gx+h||_2 <= e'x + f * *  x'(G'G - ee')x + (2 h'G - 2 f e') x +\n"
"h'h - f <= 0 * where we put e = [0 0 ... 1] for the quadratic constraint arising\n"
"from the epigraph reformulation and e==0 for all other quadratic\n"
"constraints.\n"
"\n"
"\n"
;