/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
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



#include "bonmin_interface.hpp"
#include "bonmin_nlp.hpp"
#include "casadi/core/casadi_misc.hpp"
#include "../../core/global_options.hpp"
#include "../../core/casadi_interrupt.hpp"

#include <ctime>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace std;

namespace casadi {
  extern "C"
  int CASADI_NLPSOL_BONMIN_EXPORT
  casadi_register_nlpsol_bonmin(Nlpsol::Plugin* plugin) {
    plugin->creator = BonminInterface::creator;
    plugin->name = "bonmin";
    plugin->doc = BonminInterface::meta_doc.c_str();
    plugin->version = CASADI_VERSION;
    plugin->options = &BonminInterface::options_;
    plugin->deserialize = &BonminInterface::deserialize;
    return 0;
  }

  extern "C"
  void CASADI_NLPSOL_BONMIN_EXPORT casadi_load_nlpsol_bonmin() {
    Nlpsol::registerPlugin(casadi_register_nlpsol_bonmin);
  }

  BonminInterface::BonminInterface(const std::string& name, const Function& nlp)
    : Nlpsol(name, nlp) {
  }

  BonminInterface::~BonminInterface() {
    clear_mem();
  }

  const Options BonminInterface::options_
  = {{&Nlpsol::options_},
     {{"pass_nonlinear_variables",
       {OT_BOOL,
        "Pass list of variables entering nonlinearly to BONMIN"}},
      {"pass_nonlinear_constraints",
       {OT_BOOL,
        "Pass list of constraints entering nonlinearly to BONMIN"}},
      {"bonmin",
       {OT_DICT,
        "Options to be passed to BONMIN"}},
      {"var_string_md",
       {OT_DICT,
        "String metadata (a dictionary with lists of strings) "
        "about variables to be passed to BONMIN"}},
      {"var_integer_md",
       {OT_DICT,
        "Integer metadata (a dictionary with lists of integers) "
        "about variables to be passed to BONMIN"}},
      {"var_numeric_md",
       {OT_DICT,
        "Numeric metadata (a dictionary with lists of reals) about "
        "variables to be passed to BONMIN"}},
      {"con_string_md",
       {OT_DICT,
        "String metadata (a dictionary with lists of strings) about "
        "constraints to be passed to BONMIN"}},
      {"con_integer_md",
       {OT_DICT,
        "Integer metadata (a dictionary with lists of integers) "
        "about constraints to be passed to BONMIN"}},
      {"con_numeric_md",
       {OT_DICT,
        "Numeric metadata (a dictionary with lists of reals) about "
        "constraints to be passed to BONMIN"}},
      {"hess_lag",
       {OT_FUNCTION,
        "Function for calculating the Hessian of the Lagrangian (autogenerated by default)"}},
      {"hess_lag_options",
       {OT_DICT,
        "Options for the autogenerated Hessian of the Lagrangian."}},
      {"jac_g",
       {OT_FUNCTION,
        "Function for calculating the Jacobian of the constraints "
        "(autogenerated by default)"}},
      {"jac_g_options",
       {OT_DICT,
        "Options for the autogenerated Jacobian of the constraints."}},
      {"grad_f",
       {OT_FUNCTION,
        "Function for calculating the gradient of the objective "
        "(column, autogenerated by default)"}},
      {"grad_f_options",
       {OT_DICT,
        "Options for the autogenerated gradient of the objective."}},
      {"sos1_groups",
       {OT_INTVECTORVECTOR,
        "Options for the autogenerated gradient of the objective."}},
      {"sos1_weights",
       {OT_DOUBLEVECTORVECTOR,
        "Options for the autogenerated gradient of the objective."}},
      {"sos1_priorities",
       {OT_INTVECTOR,
        "Options for the autogenerated gradient of the objective."}},
     }
  };

  void BonminInterface::init(const Dict& opts) {
    // Call the init method of the base class
    Nlpsol::init(opts);

    // Default options
    pass_nonlinear_variables_ = true;
    pass_nonlinear_constraints_ = true;
    Dict hess_lag_options, jac_g_options, grad_f_options;

    std::vector< std::vector<int> > sos1_groups;
    std::vector< std::vector<double> > sos1_weights;
    // Read user options
    for (auto&& op : opts) {
      if (op.first=="bonmin") {
        opts_ = op.second;
      } else if (op.first=="pass_nonlinear_variables") {
        pass_nonlinear_variables_ = op.second;
      } else if (op.first=="pass_nonlinear_constraints") {
        pass_nonlinear_constraints_ = op.second;
      }  else if (op.first=="var_string_md") {
        var_string_md_ = op.second;
      } else if (op.first=="var_integer_md") {
        var_integer_md_ = op.second;
      } else if (op.first=="var_numeric_md") {
        var_numeric_md_ = op.second;
      } else if (op.first=="con_string_md") {
        con_string_md_ = op.second;
      } else if (op.first=="con_integer_md") {
        con_integer_md_ = op.second;
      } else if (op.first=="con_numeric_md") {
        con_numeric_md_ = op.second;
      } else if (op.first=="hess_lag_options") {
        hess_lag_options = op.second;
      } else if (op.first=="jac_g_options") {
        jac_g_options = op.second;
      } else if (op.first=="grad_f_options") {
        grad_f_options = op.second;
      } else if (op.first=="hess_lag") {
        Function f = op.second;
        casadi_assert_dev(f.n_in()==4);
        casadi_assert_dev(f.n_out()==1);
        set_function(f, "nlp_hess_l");
      } else if (op.first=="jac_g") {
        Function f = op.second;
        casadi_assert_dev(f.n_in()==2);
        casadi_assert_dev(f.n_out()==2);
        set_function(f, "nlp_jac_g");
      } else if (op.first=="grad_f") {
        Function f = op.second;
        casadi_assert_dev(f.n_in()==2);
        casadi_assert_dev(f.n_out()==2);
        set_function(f, "nlp_grad_f");
      } else if (op.first=="sos1_groups") {
        sos1_groups = to_int(op.second.to_int_vector_vector());
        for (auto & g : sos1_groups) {
          for (auto & e : g) e-= GlobalOptions::start_index;
        }
      } else if (op.first=="sos1_weights") {
        sos1_weights = op.second.to_double_vector_vector();
      } else if (op.first=="sos1_priorities") {
        sos1_priorities_ = to_int(op.second.to_int_vector());
      }
    }

    // Do we need second order derivatives?
    exact_hessian_ = true;
    auto hessian_approximation = opts_.find("hessian_approximation");
    if (hessian_approximation!=opts_.end()) {
      exact_hessian_ = hessian_approximation->second == "exact";
    }


    // Setup NLP functions
    create_function("nlp_f", {"x", "p"}, {"f"});
    create_function("nlp_g", {"x", "p"}, {"g"});
    if (!has_function("nlp_grad_f")) {
      create_function("nlp_grad_f", {"x", "p"}, {"f", "grad:f:x"});
    }
    if (!has_function("nlp_jac_g")) {
      create_function("nlp_jac_g", {"x", "p"}, {"g", "jac:g:x"});
    }
    jacg_sp_ = get_function("nlp_jac_g").sparsity_out(1);

    // By default, assume all nonlinear
    nl_ex_.resize(nx_, true);
    nl_g_.resize(ng_, true);

    // Allocate temporary work vectors
    if (exact_hessian_) {
      if (!has_function("nlp_hess_l")) {
        create_function("nlp_hess_l", {"x", "p", "lam:f", "lam:g"},
                        {"triu:hess:gamma:x:x"}, {{"gamma", {"f", "g"}}});
      }
      hesslag_sp_ = get_function("nlp_hess_l").sparsity_out(0);

      if (pass_nonlinear_variables_) {
        const casadi_int* col = hesslag_sp_.colind();
        for (casadi_int i=0;i<nx_;++i) nl_ex_[i] = col[i+1]-col[i];
      }
    } else {
      if (pass_nonlinear_variables_)
        nl_ex_ = oracle_.which_depends("x", {"f", "g"}, 2, false);
    }
    if (pass_nonlinear_constraints_)
      nl_g_ = oracle_.which_depends("x", {"g"}, 2, true);

    // Create sos info

    // Declare size
    sos_num_ = sos1_groups.size();
    // sos1 type
    sos1_types_.resize(sos_num_, 1);

    casadi_assert(sos1_weights.empty() || sos1_weights.size()==sos_num_,
      "sos1_weights has incorrect size");
    casadi_assert(sos1_priorities_.empty() || sos1_priorities_.size()==sos_num_,
      "sos1_priorities has incorrect size");
    if (sos1_priorities_.empty()) sos1_priorities_.resize(sos_num_, 1);

    sos_num_nz_ = 0;
    for (casadi_int i=0;i<sos_num_;++i) {
      // get local group
      const std::vector<int>& sos1_group = sos1_groups[i];

      // Get local weights
      std::vector<double> default_weights(sos1_group.size(), 1.0);
      const std::vector<double>& sos1_weight =
        sos1_weights.empty() ? default_weights : sos1_weights[i];
      casadi_assert(sos1_weight.size()==sos1_group.size(),
        "sos1_weights has incorrect size");

      // Populate lookup vector
      sos1_starts_.push_back(sos_num_nz_);
      sos_num_nz_+=sos1_group.size();

      sos1_weights_.insert(sos1_weights_.end(), sos1_weight.begin(), sos1_weight.end());
      sos1_indices_.insert(sos1_indices_.end(), sos1_group.begin(), sos1_group.end());
    }

    sos1_starts_.push_back(sos_num_nz_);

    // Allocate work vectors
    alloc_w(nx_, true); // xk_
    alloc_w(nx_, true); // lam_xk_
    alloc_w(ng_, true); // gk_
    alloc_w(nx_, true); // grad_fk_
    alloc_w(jacg_sp_.nnz(), true); // jac_gk_
    if (exact_hessian_) {
      alloc_w(hesslag_sp_.nnz(), true); // hess_lk_
    }
  }

  int BonminInterface::init_mem(void* mem) const {

    auto m = static_cast<BonminMemory*>(mem);
    m->sos_info.num = sos_num_;
    m->sos_info.numNz = sos_num_nz_;
    // sos_info takes ownership of passed-in pointers
    m->sos_info.types = new char[sos_num_];
    m->sos_info.priorities = new int[sos_num_];
    m->sos_info.starts = new int[sos_num_ + 1];
    m->sos_info.indices = new int[sos_num_nz_];
    m->sos_info.weights = new double[sos_num_nz_];
    casadi_assert_dev(sos_num_==sos1_types_.size());
    casadi_assert_dev(sos_num_==sos1_priorities_.size());
    casadi_assert_dev(sos_num_+1==sos1_starts_.size());
    casadi_assert_dev(sos_num_nz_==sos1_indices_.size());
    casadi_assert_dev(sos_num_nz_==sos1_weights_.size());
    std::copy(sos1_types_.begin(), sos1_types_.end(), m->sos_info.types);
    std::copy(sos1_priorities_.begin(), sos1_priorities_.end(), m->sos_info.priorities);
    std::copy(sos1_starts_.begin(), sos1_starts_.end(), m->sos_info.starts);
    std::copy(sos1_indices_.begin(), sos1_indices_.end(), m->sos_info.indices);
    std::copy(sos1_weights_.begin(), sos1_weights_.end(), m->sos_info.weights);

    return Nlpsol::init_mem(mem);
  }

  void BonminInterface::set_work(void* mem, const double**& arg, double**& res,
                                casadi_int*& iw, double*& w) const {
    auto m = static_cast<BonminMemory*>(mem);

    // Set work in base classes
    Nlpsol::set_work(mem, arg, res, iw, w);

    // Work vectors
    m->gk = w; w += ng_;
    m->grad_fk = w; w += nx_;
    m->jac_gk = w; w += jacg_sp_.nnz();
    if (exact_hessian_) {
      m->hess_lk = w; w += hesslag_sp_.nnz();
    }
  }

  inline const char* return_status_string(Bonmin::TMINLP::SolverReturn status) {
    switch (status) {
    case Bonmin::TMINLP::MINLP_ERROR:
      return "MINLP_ERROR";
    case Bonmin::TMINLP::SUCCESS:
      return "SUCCESS";
    case Bonmin::TMINLP::INFEASIBLE:
      return "INFEASIBLE";
    case Bonmin::TMINLP::CONTINUOUS_UNBOUNDED:
      return "CONTINUOUS_UNBOUNDED";
    case Bonmin::TMINLP::LIMIT_EXCEEDED:
      return "LIMIT_EXCEEDED";
    case Bonmin::TMINLP::USER_INTERRUPT:
      return "USER_INTERRUPT";
    }
    return "Unknown";
  }

  inline std::string to_str(const CoinError& e) {
    std::stringstream ss;
    if (e.lineNumber()<0) {
      ss << e.message()<< " in "<< e.className()<< "::" << e.methodName();
    } else {
      ss << e.fileName() << ":" << e.lineNumber() << " method " << e.methodName()
         << " : assertion \'" << e.message() <<"\' failed.";
      if (!e.className().empty())
        ss <<"Possible reason: "<< e.className();
    }
    return ss.str();
  }

  inline std::string to_str(TNLPSolver::UnsolvedError& e) {
    std::stringstream ss;
    e.printError(ss);
    return ss.str();
  }


  /** \brief Helper class to direct messages to uout()
  *
  * IPOPT has the concept of a Journal/Journalist
  * BONMIN and CBC do not.
  */
  class BonMinMessageHandler : public CoinMessageHandler {
  public:
    BonMinMessageHandler() { }
    /// Core of the class: the method that directs the messages
    int print() override {
      uout() << messageBuffer_ << std::endl;
      return 0;
    }
    ~BonMinMessageHandler() override { }
    BonMinMessageHandler(const BonMinMessageHandler &other): CoinMessageHandler(other) {}
    BonMinMessageHandler(const CoinMessageHandler &other): CoinMessageHandler(other) {}
    BonMinMessageHandler & operator=(const BonMinMessageHandler &rhs) {
      CoinMessageHandler::operator=(rhs);
      return *this;
    }
    CoinMessageHandler* clone() const override {
      return new BonMinMessageHandler(*this);
    }
  };

  int BonminInterface::solve(void* mem) const {
    auto m = static_cast<BonminMemory*>(mem);
    auto d_nlp = &m->d_nlp;

    // Reset statistics
    m->inf_pr.clear();
    m->inf_du.clear();
    m->mu.clear();
    m->d_norm.clear();
    m->regularization_size.clear();
    m->alpha_pr.clear();
    m->alpha_du.clear();
    m->obj.clear();
    m->ls_trials.clear();

    // Reset number of iterations
    m->n_iter = 0;

    // MINLP instance
    SmartPtr<BonminUserClass> tminlp = new BonminUserClass(*this, m);

    BonMinMessageHandler mh;

    // Start an BONMIN application
    BonminSetup bonmin(&mh);

    SmartPtr<OptionsList> options = new OptionsList();
    SmartPtr<Journalist> journalist= new Journalist();
    SmartPtr<Bonmin::RegisteredOptions> roptions = new Bonmin::RegisteredOptions();

    {
      // Direct output through casadi::uout()
      StreamJournal* jrnl_raw = new StreamJournal("console", J_ITERSUMMARY);
      jrnl_raw->SetOutputStream(&casadi::uout());
      jrnl_raw->SetPrintLevel(J_DBG, J_NONE);
      SmartPtr<Journal> jrnl = jrnl_raw;
      journalist->AddJournal(jrnl);
    }

    options->SetJournalist(journalist);
    options->SetRegisteredOptions(roptions);
    bonmin.setOptionsAndJournalist(roptions, options, journalist);
    bonmin.registerOptions();
    // Get all options available in BONMIN
    auto regops = bonmin.roptions()->RegisteredOptionsList();

    // Pass all the options to BONMIN
    for (auto&& op : opts_) {
      // Find the option
      auto regops_it = regops.find(op.first);
      if (regops_it==regops.end()) {
        casadi_error("No such BONMIN option: " + op.first);
      }

      // Get the type
      Ipopt::RegisteredOptionType ipopt_type = regops_it->second->Type();

      // Pass to BONMIN
      bool ret;
      switch (ipopt_type) {
      case Ipopt::OT_Number:
        ret = bonmin.options()->SetNumericValue(op.first, op.second.to_double(), false);
        break;
      case Ipopt::OT_Integer:
        ret = bonmin.options()->SetIntegerValue(op.first, op.second.to_int(), false);
        break;
      case Ipopt::OT_String:
        ret = bonmin.options()->SetStringValue(op.first, op.second.to_string(), false);
        break;
      case Ipopt::OT_Unknown:
      default:
        casadi_warning("Cannot handle option \"" + op.first + "\", ignored");
        continue;
      }
      if (!ret) casadi_error("Invalid options were detected by BONMIN.");
    }

    // Initialize
    bonmin.initialize(GetRawPtr(tminlp));

    // Branch-and-bound
    try {
      Bab bb;
      bb(bonmin);
    } catch (CoinError& e) {
      casadi_error("CoinError occured: " + to_str(e));
    } catch (TNLPSolver::UnsolvedError& e) {
      casadi_error("TNLPSolver::UnsolvedError occured" + to_str(e));
    } catch (...) {
      casadi_error("Uncaught error in Bonmin");
    }

    // Save results to outputs
    casadi_copy(m->gk, ng_, d_nlp->z + nx_);
    return 0;
  }

  bool BonminInterface::
  intermediate_callback(BonminMemory* m, const double* x, const double* z_L, const double* z_U,
                        const double* g, const double* lambda, double obj_value, int iter,
                        double inf_pr, double inf_du, double mu, double d_norm,
                        double regularization_size, double alpha_du, double alpha_pr,
                        int ls_trials, bool full_callback) const {
    auto d_nlp = &m->d_nlp;
    m->n_iter += 1;
    try {
      if (verbose_) casadi_message("intermediate_callback started");
      m->inf_pr.push_back(inf_pr);
      m->inf_du.push_back(inf_du);
      m->mu.push_back(mu);
      m->d_norm.push_back(d_norm);
      m->regularization_size.push_back(regularization_size);
      m->alpha_pr.push_back(alpha_pr);
      m->alpha_du.push_back(alpha_du);
      m->ls_trials.push_back(ls_trials);
      m->obj.push_back(obj_value);
      if (!fcallback_.is_null()) {
        ScopedTiming tic(m->fstats.at("callback_fun"));
        if (full_callback) {
          casadi_copy(x, nx_, d_nlp->z);
          for (casadi_int i=0; i<nx_; ++i) {
            d_nlp->lam[i] = z_U[i]-z_L[i];
          }
          casadi_copy(lambda, ng_, d_nlp->lam + nx_);
          casadi_copy(g, ng_, m->gk);
        } else {
          if (iter==0) {
            uerr()
              << "Warning: intermediate_callback is disfunctional in your installation. "
              "You will only be able to use stats(). "
              "See https://github.com/casadi/casadi/wiki/enableBonminCallback to enable it."
              << std::endl;
          }
        }

        // Inputs
        std::fill_n(m->arg, fcallback_.n_in(), nullptr);
        if (full_callback) {
          // The values used below are meaningless
          // when not doing a full_callback
          m->arg[NLPSOL_X] = x;
          m->arg[NLPSOL_F] = &obj_value;
          m->arg[NLPSOL_G] = g;
          m->arg[NLPSOL_LAM_P] = nullptr;
          m->arg[NLPSOL_LAM_X] = d_nlp->lam;
          m->arg[NLPSOL_LAM_G] = d_nlp->lam + nx_;
        }

        // Outputs
        std::fill_n(m->res, fcallback_.n_out(), nullptr);
        double ret_double;
        m->res[0] = &ret_double;

        fcallback_(m->arg, m->res, m->iw, m->w, 0);
        int ret = static_cast<int>(ret_double);

        m->fstats.at("callback_fun").toc();
        return  !ret;
      } else {
        return 1;
      }
    } catch(KeyboardInterruptException& ex) {
      return 0;
    } catch(std::exception& ex) {
      if (iteration_callback_ignore_errors_) {
        uerr() << "intermediate_callback: " << ex.what() << std::endl;
        return 1;
      }
      return 0;
    }
  }

  void BonminInterface::
  finalize_solution(BonminMemory* m, TMINLP::SolverReturn status,
      const double* x, double obj_value) const {
    auto d_nlp = &m->d_nlp;
    try {
      // Get primal solution
      casadi_copy(x, nx_, d_nlp->z);

      // Get optimal cost
      d_nlp->objective = obj_value;

      // Dual solution not calculated
      casadi_fill(d_nlp->lam, nx_ + ng_, nan);

      // Get the constraints
      casadi_fill(m->gk, ng_, nan);

      // Get statistics
      m->iter_count = 0;

      // Interpret return code
      m->return_status = return_status_string(status);
      m->success = status==Bonmin::TMINLP::SUCCESS;
      if (status==Bonmin::TMINLP::LIMIT_EXCEEDED) m->unified_return_status = SOLVER_RET_LIMITED;
    } catch(std::exception& ex) {
      uerr() << "finalize_solution failed: " << ex.what() << std::endl;
    }
  }

  const TMINLP::SosInfo& BonminInterface::sosConstraints(BonminMemory* m) const {
    return m->sos_info;
  }

  bool BonminInterface::
  get_bounds_info(BonminMemory* m, double* x_l, double* x_u,
                  double* g_l, double* g_u) const {
    auto d_nlp = &m->d_nlp;
    try {
      casadi_copy(d_nlp->lbz, nx_, x_l);
      casadi_copy(d_nlp->ubz, nx_, x_u);
      casadi_copy(d_nlp->lbz+nx_, ng_, g_l);
      casadi_copy(d_nlp->ubz+nx_, ng_, g_u);
      return true;
    } catch(std::exception& ex) {
      uerr() << "get_bounds_info failed: " << ex.what() << std::endl;
      return false;
    }
  }

  bool BonminInterface::
  get_starting_point(BonminMemory* m, bool init_x, double* x,
                     bool init_z, double* z_L, double* z_U,
                     bool init_lambda, double* lambda) const {
    auto d_nlp = &m->d_nlp;
    try {
      // Initialize primal variables
      if (init_x) {
        casadi_copy(d_nlp->z, nx_, x);
      }

      // Initialize dual variables (simple bounds)
      if (init_z) {
        for (casadi_int i=0; i<nx_; ++i) {
          z_L[i] = std::max(0., -d_nlp->lam[i]);
          z_U[i] = std::max(0., d_nlp->lam[i]);
        }
      }

      // Initialize dual variables (nonlinear bounds)
      if (init_lambda) {
        casadi_copy(d_nlp->lam + nx_, ng_, lambda);
      }

      return true;
    } catch(std::exception& ex) {
      uerr() << "get_starting_point failed: " << ex.what() << std::endl;
      return false;
    }
  }

  void BonminInterface::get_nlp_info(BonminMemory* m, int& nx, int& ng,
                                    int& nnz_jac_g, int& nnz_h_lag) const {
    try {
      // Number of variables
      nx = nx_;

      // Number of constraints
      ng = ng_;

      // Number of Jacobian nonzeros
      nnz_jac_g = ng_==0 ? 0 : jacg_sp_.nnz();

      // Number of Hessian nonzeros (only upper triangular half)
      nnz_h_lag = exact_hessian_ ? hesslag_sp_.nnz() : 0;

    } catch(std::exception& ex) {
      uerr() << "get_nlp_info failed: " << ex.what() << std::endl;
    }
  }

  int BonminInterface::get_number_of_nonlinear_variables() const {
    try {
      if (!pass_nonlinear_variables_) {
        // No Hessian has been interfaced
        return -1;
      } else {
        // Number of variables that appear nonlinearily
        int nv = 0;
        for (auto&& i : nl_ex_) if (i) nv++;
        return nv;
      }
    } catch(std::exception& ex) {
      uerr() << "get_number_of_nonlinear_variables failed: " << ex.what() << std::endl;
      return -1;
    }
  }

  bool BonminInterface::
  get_list_of_nonlinear_variables(int num_nonlin_vars, int* pos_nonlin_vars) const {
    try {
      for (casadi_int i=0; i<nl_ex_.size(); ++i) {
        if (nl_ex_[i]) *pos_nonlin_vars++ = i;
      }
      return true;
    } catch(std::exception& ex) {
      uerr() << "get_list_of_nonlinear_variables failed: " << ex.what() << std::endl;
      return false;
    }
  }

  BonminMemory::BonminMemory() {
    this->return_status = "Unset";
  }

  BonminMemory::~BonminMemory() {
  }

  Dict BonminInterface::get_stats(void* mem) const {
    Dict stats = Nlpsol::get_stats(mem);
    auto m = static_cast<BonminMemory*>(mem);
    stats["return_status"] = m->return_status;
    stats["iter_count"] = m->iter_count;
    return stats;
  }

  BonminInterface::BonminInterface(DeserializingStream& s) : Nlpsol(s) {
    s.version("BonminInterface", 1);
    s.unpack("BonminInterface::jacg_sp", jacg_sp_);
    s.unpack("BonminInterface::hesslag_sp", hesslag_sp_);
    s.unpack("BonminInterface::exact_hessian", exact_hessian_);
    s.unpack("BonminInterface::opts", opts_);

    s.unpack("BonminInterface::sos1_weights", sos1_weights_);
    s.unpack("BonminInterface::sos1_indices", sos1_indices_);
    s.unpack("BonminInterface::sos1_priorities", sos1_priorities_);
    s.unpack("BonminInterface::sos1_starts", sos1_starts_);
    s.unpack("BonminInterface::sos1_types", sos1_types_);
    s.unpack("BonminInterface::sos1_types", sos1_types_);
    s.unpack("BonminInterface::sos_num", sos_num_);
    s.unpack("BonminInterface::sos_num_nz", sos_num_nz_);

    s.unpack("BonminInterface::pass_nonlinear_variables", pass_nonlinear_variables_);
    s.unpack("BonminInterface::pass_nonlinear_constraints", pass_nonlinear_constraints_);
    s.unpack("BonminInterface::nl_ex", nl_ex_);
    s.unpack("BonminInterface::nl_g", nl_g_);
    s.unpack("BonminInterface::var_string_md", var_string_md_);
    s.unpack("BonminInterface::var_integer_md", var_integer_md_);
    s.unpack("BonminInterface::var_numeric_md", var_numeric_md_);
    s.unpack("BonminInterface::con_string_md", con_string_md_);
    s.unpack("BonminInterface::con_integer_md", con_integer_md_);
    s.unpack("BonminInterface::con_numeric_md", con_numeric_md_);
  }

  void BonminInterface::serialize_body(SerializingStream &s) const {
    Nlpsol::serialize_body(s);
    s.version("BonminInterface", 1);
    s.pack("BonminInterface::jacg_sp", jacg_sp_);
    s.pack("BonminInterface::hesslag_sp", hesslag_sp_);
    s.pack("BonminInterface::exact_hessian", exact_hessian_);
    s.pack("BonminInterface::opts", opts_);

    s.pack("BonminInterface::sos1_weights", sos1_weights_);
    s.pack("BonminInterface::sos1_indices", sos1_indices_);
    s.pack("BonminInterface::sos1_priorities", sos1_priorities_);
    s.pack("BonminInterface::sos1_starts", sos1_starts_);
    s.pack("BonminInterface::sos1_types", sos1_types_);
    s.pack("BonminInterface::sos1_types", sos1_types_);
    s.pack("BonminInterface::sos_num", sos_num_);
    s.pack("BonminInterface::sos_num_nz", sos_num_nz_);

    s.pack("BonminInterface::pass_nonlinear_variables", pass_nonlinear_variables_);
    s.pack("BonminInterface::pass_nonlinear_constraints", pass_nonlinear_constraints_);
    s.pack("BonminInterface::nl_ex", nl_ex_);
    s.pack("BonminInterface::nl_g", nl_g_);
    s.pack("BonminInterface::var_string_md", var_string_md_);
    s.pack("BonminInterface::var_integer_md", var_integer_md_);
    s.pack("BonminInterface::var_numeric_md", var_numeric_md_);
    s.pack("BonminInterface::con_string_md", con_string_md_);
    s.pack("BonminInterface::con_integer_md", con_integer_md_);
    s.pack("BonminInterface::con_numeric_md", con_numeric_md_);
  }

} // namespace casadi
