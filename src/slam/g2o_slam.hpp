//
//  g2o_slam.hpp
//  slam
//
//  Created by Roshan Shariff on 2012-08-07.
//  Copyright (c) 2012 University of Alberta. All rights reserved.
//

#ifndef slam_g2o_slam_hpp
#define slam_g2o_slam_hpp

#include <memory>
#include <iostream>
#include <cassert>

#include <boost/program_options.hpp>

#include <Eigen/Core>

#include <g2o/core/base_vertex.h>
#include <g2o/core/base_binary_edge.h>
#include <g2o/core/sparse_optimizer.h>
#include <g2o/core/sparse_optimizer_terminate_action.h>
#include <g2o/core/hyper_graph.h>
#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_gauss_newton.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/solvers/csparse/linear_solver_csparse.h>

#include "slam/interfaces.hpp"
#include "slam/slam_data.hpp"
#include "utility/bitree.hpp"
#include "utility/random.hpp"
#include "utility/utility.hpp"

#include "main.hpp"


namespace slam {    
    
    template <class ControlModel, class ObservationModel>
    class g2o_slam :
    public slam_result_of<ControlModel, ObservationModel>,
    public slam_data<ControlModel, ObservationModel>::listener
    {
        
        using slam_result_type = slam_result_of<ControlModel, ObservationModel>;
        using slam_data_type = slam_data<ControlModel, ObservationModel>;
        
        /** See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58047 for why the redundant
         ** "state_type = " is needed here in g++ 4.8.1 */

        using state_type = typename slam_result_type::state_type;
        using feature_type = typename slam_result_type::feature_type;
        using typename slam_result_type::trajectory_type;
        using typename slam_result_type::feature_map_type;

        using control_type = typename ControlModel::vector_type;
        using observation_type = typename ObservationModel::vector_type;
        
        /** Nested types */
        
        struct vertex_state : public g2o::BaseVertex<state_type::vector_dim, state_type> {
            
            EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
            
            vertex_state () = default;
            
            vertex_state (int id, const state_type& estimate) {
                this->setId (id);
                this->setEstimate (estimate);
            }
            
            virtual auto read (std::istream&) -> bool { return true; }
            virtual auto write (std::ostream&) const -> bool { return true; }
            
        protected:
            
            virtual void setToOriginImpl () override {
                this->setEstimate (state_type());
            }
            
            virtual void oplusImpl (const double* update) override {
                using map_type = Eigen::Map<const typename state_type::vector_type>;
                this->setEstimate(this->estimate() + state_type::from_vector (map_type (update)));
            }
        };
        
        
        struct vertex_landmark : public g2o::BaseVertex<feature_type::vector_dim, feature_type> {
            
            EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
            
            vertex_landmark () = default;
            
            vertex_landmark (int id, const feature_type& estimate) {
                this->setId (id);
                this->setEstimate (estimate);
                //this->setMarginalized (true);
            }
            
            virtual auto read (std::istream&) -> bool { return true; }
            virtual auto write (std::ostream&) const -> bool { return true; }
            
        protected:
            
            virtual void setToOriginImpl () override {
                this->setEstimate (feature_type());
            }
            
            virtual void oplusImpl (const double* update) override {
                using map_type = Eigen::Map<const typename feature_type::vector_type>;
                this->setEstimate (feature_type::from_vector (this->estimate().to_vector() + map_type(update)));
            }
        };
        
        
        struct edge_control
        : public g2o::BaseBinaryEdge<ControlModel::vector_dim, control_type, vertex_state, vertex_state> {
            
            EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

            edge_control (vertex_state* v0, vertex_state* v1, const ControlModel& control) {

                this->setVertex (0, v0);
                this->setVertex (1, v1);
                this->setMeasurement (control.mean());

                typename ControlModel::matrix_type chol_cov_inv;
                chol_cov_inv.setIdentity();
                control.chol_cov().template triangularView<Eigen::Lower>().solveInPlace(chol_cov_inv);
                this->setInformation (chol_cov_inv.transpose() * chol_cov_inv);
            }
            
            virtual auto read (std::istream&) -> bool { return true; }
            virtual auto write (std::ostream&) const -> bool { return true; }
            
        protected:
            
            void computeError () override {
                auto v1 = static_cast<const vertex_state*> (this->vertex(0));
                auto v2 = static_cast<const vertex_state*> (this->vertex(1));
                control_type predicted = ControlModel::observe (-v1->estimate() + v2->estimate());
                this->error() = ControlModel::subtract (predicted, this->measurement());
            }
        };
        
        
        struct edge_obs
        : public g2o::BaseBinaryEdge<ObservationModel::vector_dim, observation_type, vertex_state, vertex_landmark> {
          
            EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
            
            edge_obs (vertex_state* v0, vertex_landmark* v1, const ObservationModel& obs) {

                this->setVertex (0, v0);
                this->setVertex (1, v1);
                this->setMeasurement (obs.mean());
                
                typename ObservationModel::matrix_type chol_cov_inv;
                chol_cov_inv.setIdentity();
                obs.chol_cov().template triangularView<Eigen::Lower>().solveInPlace(chol_cov_inv);
                this->setInformation (chol_cov_inv.transpose() * chol_cov_inv);
            }
            
            virtual auto read (std::istream&) -> bool { return true; }
            virtual auto write (std::ostream&) const -> bool { return true; }
            
        protected:
            
            void computeError () override {
                auto v1 = static_cast<const vertex_state*> (this->vertex(0));
                auto v2 = static_cast<const vertex_landmark*> (this->vertex(1));
                observation_type predicted = ObservationModel::observe (-v1->estimate() + v2->estimate());
                this->error() = ObservationModel::subtract (predicted, this->measurement());
            }
        };
                
        
        /** Private data members */
        
        std::shared_ptr<slam_result_type> initialiser;
        
        g2o::SparseOptimizerTerminateAction optimizer_terminate_action;
        bool optimizer_force_stop_flag = false;
        
        g2o::SparseOptimizer optimizer;
        g2o::HyperGraph::VertexSet new_vertices;
        g2o::HyperGraph::EdgeSet new_edges;
        bool optimizer_need_init = true;
        
        std::vector<vertex_state*> state_vertices;
        std::map<featureid_type, vertex_landmark*> feature_vertices;
        int next_vertex_id = 0;
        
        mutable trajectory_type trajectory_estimate;
        mutable feature_map_type map_estimate;
        
        timestep_type next_timestep;
        
    public:
        
        explicit g2o_slam (const decltype(initialiser)&);

        static boost::program_options::options_description program_options ();
        
        void reinitialise (const slam_result_type&);
        
        int optimise (int iterations = 1000);
        
        /** Overridden virtual member functions of slam::slam_data::listener */

        virtual void control (timestep_type t, const ControlModel& control) override;
        virtual void observation (timestep_type t, const typename slam_data_type::observation_info& obs) override;
        
        virtual void timestep (timestep_type) override;
        
        /** Overridden virtual member functions of slam::slam_result */
        
        virtual auto current_timestep () const -> timestep_type override {
            assert (next_timestep > 0);
            return next_timestep - 1;
        }
        
        virtual auto get_state (timestep_type t) const -> state_type override {
            return state_vertices.at(t)->estimate();
        }
        
        virtual auto get_feature (featureid_type id) const -> feature_type override {
            return feature_vertices.at(id)->estimate();
        }
        
        virtual auto get_trajectory () const -> const trajectory_type& override;
        
        virtual auto get_feature_map () const -> const feature_map_type& override;
        
        double objective_value () const { return optimizer.activeRobustChi2(); }
        
        class updater : public timestep_listener {
            
            std::shared_ptr<g2o_slam> instance;
            unsigned int steps, end_steps;
            
        public:
            
            updater (const decltype(instance)& instance, unsigned int steps = 0, unsigned int end_steps = 0)
            : instance(instance), steps(steps), end_steps(end_steps) { }
            
            updater (const decltype(instance)&, const boost::program_options::variables_map&);
            
            virtual void timestep (timestep_type t) override {
                instance->timestep (t);
                /*const int iterations =*/ instance->optimise (steps);
                //std::cout << "G2O iterations: " << iterations << '\n';
            }
            
            virtual void completed () override {
                instance->completed();
                /*const int iterations =*/ instance->optimise (end_steps);
                //std::cout << "G2O iterations: " << iterations << '\n';
            }
        };
        
    };
    
} // namespace slam


template <class ControlModel, class ObservationModel>
void slam::g2o_slam<ControlModel, ObservationModel>
::reinitialise (const slam_result_type& initialiser) {
    
    const auto& trajectory = initialiser.get_trajectory();
    for (timestep_type t (1); t <= trajectory.size(); ++t) {
        state_vertices.at(t)->setEstimate (trajectory.accumulate (t));
    }
    trajectory_estimate.clear();
    
    const auto& initial_state = initialiser.get_initial_state();
    for (const auto& feature : initialiser.get_feature_map()) {
        feature_vertices.at(feature.first)->setEstimate (-initial_state + feature.second);
    }
    map_estimate.clear();
}


template <class ControlModel, class ObservationModel>
auto slam::g2o_slam<ControlModel, ObservationModel>
::optimise (const int max_iterations) -> int {
    
    if (max_iterations <= 0 || state_vertices.size() <= 1 || feature_vertices.empty()) {
        return 0;
    }
    
    if (optimizer_need_init) {
        optimizer.initializeOptimization();
        optimizer_need_init = false;
    }
    else {
        optimizer.updateInitialization (new_vertices, new_edges);
    }
    
    new_vertices.clear();
    new_edges.clear();
    
    const int iterations = optimizer.optimize(max_iterations, false);
    optimizer_force_stop_flag = false;
    
    trajectory_estimate.clear();
    map_estimate.clear();
    
    return iterations;
}


template <class ControlModel, class ObservationModel>
void slam::g2o_slam<ControlModel, ObservationModel>
::control (timestep_type t, const ControlModel& control) {
    
    assert (t == current_timestep());
    assert (t == state_vertices.size()-1);
    assert (initialiser);
    
    state_type initial_estimate = -initialiser->get_state(t) + initialiser->get_state(t+1);
    
    auto v0 = state_vertices.back();
    auto v1 = utility::make_unique<vertex_state>(next_vertex_id++, v0->estimate() + initial_estimate);
    state_vertices.push_back (v1.get());
    
    auto e = utility::make_unique<edge_control>(v0, v1.get(), control);
    
    new_vertices.insert (v1.get());
    optimizer.addVertex (v1.release());

    new_edges.insert (e.get());
    optimizer.addEdge (e.release());
}    


template <class ControlModel, class ObservationModel>
void slam::g2o_slam<ControlModel, ObservationModel>
::observation (timestep_type t, const typename slam_data_type::observation_info& obs) {
    
    assert (t == next_timestep);
    assert (t == state_vertices.size()-1);

    auto v0 = state_vertices.back();
    auto v1 = feature_vertices[obs.id()];
    
    if (obs.index() == 0) {
    
        assert (initialiser);
        feature_type initial_estimate = -initialiser->get_state(t) + initialiser->get_feature (obs.id());
        
        auto v = utility::make_unique<vertex_landmark>(next_vertex_id++, v0->estimate() + initial_estimate);
        feature_vertices[obs.id()] = v.get();
        
        v1 = v.get();
        new_vertices.insert (v.get());
        optimizer.addVertex (v.release());
    }
    
    auto e = utility::make_unique<edge_obs>(v0, v1, obs.observation());
    new_edges.insert (e.get());
    optimizer.addEdge (e.release());
}


template <class ControlModel, class ObservationModel>
void slam::g2o_slam<ControlModel, ObservationModel>
::timestep (timestep_type t) {
    
    if (t < next_timestep) return;
    assert (t == next_timestep);    
    ++next_timestep;
}


template <class ControlModel, class ObservationModel>
auto slam::g2o_slam<ControlModel, ObservationModel>
::get_trajectory () const -> const trajectory_type& {
    
    if (trajectory_estimate.size() != current_timestep()) {
        
        trajectory_estimate.clear();
        trajectory_estimate.reserve (current_timestep());
        
        for (timestep_type t (1); t <= current_timestep(); ++t) {
            trajectory_estimate.push_back_accumulated (get_state (t));
        }
    }
    
    assert (trajectory_estimate.size() == current_timestep());
    return trajectory_estimate;
}


template <class ControlModel, class ObservationModel>
auto slam::g2o_slam<ControlModel, ObservationModel>
::get_feature_map () const -> const feature_map_type& {
    
    if (map_estimate.size() != feature_vertices.size()) {
        
        map_estimate.clear();
        map_estimate.reserve (feature_vertices.size());
        
        for (const auto& entry : feature_vertices) {
            map_estimate.emplace_hint (map_estimate.end(), entry.first, entry.second->estimate());
        }
    }
    
    assert (map_estimate.size() == feature_vertices.size());
    return map_estimate;
}


template <class ControlModel, class ObservationModel>
slam::g2o_slam<ControlModel, ObservationModel>
::g2o_slam (const decltype(initialiser)& init) : initialiser(init)
{
    
    using SlamBlockSolver = g2o::BlockSolver<g2o::BlockSolverTraits<-1, -1>>;
    using SlamLinearSolver = g2o::LinearSolverCSparse<SlamBlockSolver::PoseMatrixType>;
//    using OptimizationAlgorithm = g2o::OptimizationAlgorithmGaussNewton;
    using OptimizationAlgorithm = g2o::OptimizationAlgorithmLevenberg;
    
    auto linearSolver = utility::make_unique<SlamLinearSolver>();
    linearSolver->setBlockOrdering(false);
    
    auto blockSolver = utility::make_unique<SlamBlockSolver>(linearSolver.release());
    
    auto solver = utility::make_unique<OptimizationAlgorithm>(blockSolver.release());
    optimizer.setAlgorithm(solver.release());
    
    auto v = utility::make_unique<vertex_state>(next_vertex_id++, state_type());
    v->setFixed (true);
    
    state_vertices.push_back (v.get());
    optimizer.addVertex (v.release());
    
    optimizer_terminate_action.setGainThreshold(1e-8);
    optimizer.setForceStopFlag (&optimizer_force_stop_flag);
    optimizer.addPostIterationAction (&optimizer_terminate_action);
}


template <class ControlModel, class ObservationModel>
auto slam::g2o_slam<ControlModel, ObservationModel>
::program_options () -> boost::program_options::options_description {
    namespace po = boost::program_options;
    po::options_description options ("G2O-SLAM Parameters");
    options.add_options()
    ("g2o-steps", po::value<unsigned int>()->default_value(0), "G2O iterations per time step")
    ("g2o-end-steps", po::value<unsigned int>()->default_value(0), "G2O iterations after simulation");
    return options;
}


template <class ControlModel, class ObservationModel>
slam::g2o_slam<ControlModel, ObservationModel>::updater
::updater (const decltype(instance)& instance, const boost::program_options::variables_map& options)
: updater (instance, options["g2o-steps"].as<unsigned int>(), options["g2o-end-steps"].as<unsigned int>())
{ }


extern template class slam::g2o_slam<control_model_type, observation_model_type>;

#endif
