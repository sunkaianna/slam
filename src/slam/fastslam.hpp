//
//  fastslam.hpp
//  slam
//
//  Created by Roshan Shariff on 12-01-16.
//  Copyright (c) 2012 University of Alberta. All rights reserved.
//

#ifndef slam_fastslam_hpp
#define slam_fastslam_hpp

#include <vector>
#include <cmath>
#include <functional>

#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/none.hpp>

#include <Eigen/Eigen>

#include "slam/interfaces.hpp"
#include "slam/vector_model.hpp"
#include "slam/vector_transforms.hpp"
#include "slam/particle_filter.hpp"
#include "utility/random.hpp"
#include "utility/unscented.hpp"
#include "utility/cowmap.hpp"
#include "utility/flat_map.hpp"
#include "utility/bitree.hpp"
#include "utility/utility.hpp"


namespace slam {
    
    template <class ControlModel, class ObservationModel>
    class fastslam :
    public slam_result_of<ControlModel, ObservationModel>,
    public slam_data<ControlModel, ObservationModel>::listener
    {
        
        using state_type = typename ControlModel::result_type;
        using feature_type = typename ObservationModel::result_type;
        
        using slam_data_type = slam_data<ControlModel, ObservationModel>;
        using observation_data_type = typename slam_data_type::observation_data_type;
        
        using vec = vector_transform_functors<ControlModel, ObservationModel>;
        
        /** Distributions over states and features respectively. */
        using state_dist = vector_model_adapter<multivariate_normal_adapter<state_type>>;
        using feature_dist = vector_model_adapter<multivariate_normal_adapter<feature_type>>;
        
        /** Nested types */
        
        struct particle_type {
            struct state_list {
                state_type state;
                boost::shared_ptr<const state_list> previous;
            } trajectory;
            cowmap<featureid_type, feature_dist> features;
        };
        
        struct observed_feature_type {
            featureid_type id;
            ObservationModel observation;
            observed_feature_type (featureid_type id, const ObservationModel& obs)
            : id(id), observation(obs) { }
        };
        
        
        /* Private member functions */
        
        double particle_state_update (particle_type&) const;
        double particle_log_weight (const particle_type&) const;
        
        
        /** Private data members */
        
        /** Our very own pseudo-random number generator. */
        mutable random_source random;
        
        /** Current timestep, current control, and observations made in the current timestep. */
        timestep_type next_timestep;
        boost::optional<ControlModel> current_control;
        std::vector<observed_feature_type> seen_features, new_features;
        std::size_t num_features = 0;
        
        /** The particle filter, its target size, and the resample threshold. */
        particle_filter<particle_type> particles;
        size_t num_particles;
        double resample_threshold;
        
        /** Whether to keep a per-particle trajectory as opposed to one combined trajectory. */
        const bool discard_history;
        mutable utility::bitree<state_type> trajectory_estimate;
        mutable utility::flat_map<featureid_type, feature_type> map_estimate;
        
        /** All the UKF parameters used by FastSLAM */
        const struct unscented_params_holder {
            unscented_params<vec::control_dim> control;
            unscented_params<vec::observation_dim> obs;
            unscented_params<vec::feature_dim> feature;
            unscented_params<vec::state_dim+vec::feature_dim> state_feature;
            unscented_params_holder (double alpha, double beta, double kappa)
            : control(alpha, beta, kappa),
            obs(alpha, beta, kappa),
            feature(alpha, beta, kappa),
            state_feature(alpha, beta, kappa) { }
        } ukf_params;
        
    public:
        
        fastslam (const fastslam&) = delete;
        fastslam& operator= (const fastslam&) = delete;
        
        fastslam (boost::program_options::variables_map& options, unsigned int seed);
        
        static boost::program_options::options_description program_options ();
        
        // Overridden virtual member functions of slam::slam_result
        
        virtual void timestep (timestep_type) override;
        
        virtual timestep_type current_timestep () const override {
            assert (next_timestep > 0);
            return next_timestep - 1;
        }
        
        virtual state_type get_state (timestep_type t) const override;
        
        virtual feature_type get_feature (featureid_type id) const override {
            return particles.max_weight_particle().features.get(id).mean();
        }
        
        virtual const decltype(trajectory_estimate)& get_trajectory () const override;
        
        virtual const decltype(map_estimate)& get_feature_map () const override;
        
        // Overridden virtual member functions of slam::slam_data::listener
        
        virtual void control (timestep_type t, const ControlModel& control) override {
            assert (t == current_timestep());
            assert (!current_control);
            current_control = control;
        }
        
        virtual void observation (timestep_type t, const observation_data_type& obs) override {
            assert (t == next_timestep);
            auto& features = obs.index() == 0 ? new_features : seen_features;
            features.push_back (observed_feature_type(obs.feature_id(), obs.observation()));
        }
        
    };
    
} // namespace slam


template <class ControlModel, class ObservationModel>
void slam::fastslam<ControlModel, ObservationModel>
::timestep (timestep_type timestep) {
    
    if (timestep < next_timestep) return;
    assert (timestep == next_timestep);
    
    // Update particle state
    
    if (timestep > 0) {
        
        if (particles.effective_size() < num_particles*resample_threshold) {
            particles.resample (random, num_particles);
        }
        
        assert (current_control);
        particles.update (boost::bind (&fastslam::particle_state_update, this, _1));
        current_control = boost::none;
        
        const state_type& state_estimate = particles.max_weight_particle().trajectory.state;
        
        if (discard_history) {
            trajectory_estimate.push_back (-trajectory_estimate.accumulate() + state_estimate);
        }

        assert ((trajectory_estimate.size() == timestep) == discard_history);
    }
    
    // Update particle features
    
    for (const auto& obs : seen_features) {
        for (size_t i = 0; i < particles.size(); ++i) {
            
            const state_type& state = particles[i].trajectory.state;
            
            feature_dist feature = particles[i].features.get (obs.id);
            unscented_update (ukf_params.feature, typename vec::feature_observer(state),
                              feature.vector_model(), obs.observation.vector_model());
            particles[i].features.insert (obs.id, feature);
        }
    }
    seen_features.clear();
    
    // Initialize new features
    
    for (const auto& obs : new_features) {
        for (size_t i = 0; i < particles.size(); ++i) {
            
            const state_type& state = particles[i].trajectory.state;
            
            feature_dist feature;
            unscented_transform (ukf_params.obs, typename vec::feature_initializer(state),
                                 obs.observation.vector_model(), feature.vector_model());
            particles[i].features.insert (obs.id, feature);
        }
    }
    num_features += new_features.size();
    new_features.clear();
    
    map_estimate.clear();
    ++next_timestep;
}



template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::particle_state_update (particle_type& particle) const -> double {
    
    state_dist state, state_proposal;
    
    unscented_transform (ukf_params.control, typename vec::state_predictor(particle.trajectory.state),
                         current_control->vector_model(), state.vector_model());
    
    { // Calculate proposal distribution
        
        multivariate_normal_dist<vec::state_dim+vec::feature_dim> state_feature_joint;
        state_feature_joint.mean().template head<vec::state_dim>() = state.vector_model().mean();
        state_feature_joint.chol_cov().template topLeftCorner<vec::state_dim, vec::state_dim>() = state.vector_model().chol_cov();
        
        for (const auto& obs : seen_features) {
            
            const feature_dist& feature = particle.features.get (obs.id);
            
            state_feature_joint.mean().template tail<vec::feature_dim>() = feature.vector_model().mean();
            state_feature_joint.chol_cov().template bottomRightCorner<vec::feature_dim, vec::feature_dim>() = feature.vector_model().chol_cov();
            
            state_feature_joint.chol_cov().template topRightCorner<vec::state_dim, vec::feature_dim>().setZero();
            state_feature_joint.chol_cov().template bottomLeftCorner<vec::feature_dim, vec::state_dim>().setZero();
            
            unscented_update (ukf_params.state_feature, typename vec::state_feature_observer(),
                              state_feature_joint, obs.observation.vector_model());
        }
        
        state_proposal.vector_model().mean() = state_feature_joint.mean().template head<vec::state_dim>();
        state_proposal.vector_model().chol_cov() = state_feature_joint.chol_cov().template topLeftCorner<vec::state_dim, vec::state_dim>();
    }
    
    if (!discard_history) {
        particle.trajectory.previous = boost::make_shared<typename particle_type::state_list> (particle.trajectory);
    }
    
    particle.trajectory.state = state_proposal (random);
    
    return std::exp (particle_log_weight (particle)
                     + state.log_likelihood (particle.trajectory.state)
                     - state_proposal.log_likelihood (particle.trajectory.state));
}


template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::particle_log_weight (const particle_type& particle) const -> double {
    
    double log_weight = 0;
    
    for (const auto& obs : seen_features) {
        
        const feature_dist& feature = particle.features.get (obs.id);
        
        multivariate_normal_adapter<typename ObservationModel::vector_model_type> predicted_obs;
        unscented_transform (ukf_params.feature, typename vec::feature_observer (particle.trajectory.state),
                             feature.vector_model(), predicted_obs, obs.observation.vector_model().derived().chol_cov());
        
        log_weight += predicted_obs.log_likelihood (obs.observation.vector_model().mean());
    }
    
    return log_weight;
}


template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::get_state (timestep_type timestep) const -> state_type {
    
    assert (timestep <= current_timestep());

    if (!discard_history && trajectory_estimate.size() != current_timestep()) {
        
        const typename particle_type::state_list* p = &particles.max_weight_particle().trajectory;
        
        for (timestep_type t = current_timestep(); t > timestep; --t) {
            p = p->previous.get();
            assert(p);
        }

        return p->state;
    }
    else {
        return trajectory_estimate.accumulate (timestep);
    }
}


template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::get_trajectory () const -> const decltype(trajectory_estimate)& {
    
    if (!discard_history && trajectory_estimate.size() != current_timestep()) {
        
        trajectory_estimate.clear();
        trajectory_estimate.resize (current_timestep());
        
        const typename particle_type::state_list* p = &particles.max_weight_particle().trajectory;
        
        for (timestep_type t = current_timestep(); t > 0; --t) {
            assert (p->previous);
            trajectory_estimate[t-1] = -p->previous->state + p->state;
            p = p->previous.get();
        }
    }
    
    assert (trajectory_estimate.size() == current_timestep());
    return trajectory_estimate;
}


template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::get_feature_map () const -> const decltype(map_estimate)& {
    
    if (map_estimate.size() != num_features) {
        
        map_estimate.clear();
        map_estimate.reserve(num_features);
        
        struct {
            decltype(map_estimate)& map;
            void operator() (featureid_type id, const feature_dist& estimate) const {
                this->map.emplace_hint (this->map.end(), id, estimate.mean());
            }
        } map_estimate_inserter = { map_estimate };
        
        particles.max_weight_particle().features.for_each (map_estimate_inserter);
    }
    
    assert (map_estimate.size() == num_features);
    return map_estimate;
}


template <class ControlModel, class ObservationModel>
auto slam::fastslam<ControlModel, ObservationModel>
::program_options () -> boost::program_options::options_description {
    namespace po = boost::program_options;
    po::options_description options ("FastSLAM 2.0 Parameters");
    options.add_options()
    ("num-particles", po::value<size_t>()->default_value(10), "Number of particles in the particle filter")
    ("resample-threshold", po::value<double>()->default_value(0.5), "Minimum ratio of effective particles")
    ("no-history", "Don't keep per-particle trajectory information")
    ("ukf-alpha", po::value<double>()->default_value(0.002), "The alpha parameter for the scaled UKF")
    ("ukf-beta", po::value<double>()->default_value(2), "The beta parameter for the scaled UKF")
    ("ukf-kappa", po::value<double>()->default_value(0), "The kappa parameter for the scaled UKF")
    ("fastslam-seed", po::value<unsigned int>(), "FastSLAM 2.0 random seed");
    return options;
}


template <class ControlModel, class ObservationModel>
slam::fastslam<ControlModel, ObservationModel>
::fastslam (boost::program_options::variables_map& options, unsigned int seed)
: random           (remember_option (options, "fastslam-seed", seed)),
num_particles      (options["num-particles"].as<size_t>()),
resample_threshold (options["resample-threshold"].as<double>()),
discard_history    (options.count("no-history")),
ukf_params         (options["ukf-alpha"].as<double>(),
                    options["ukf-beta"].as<double>(),
                    options["ukf-kappa"].as<double>())
{ }


#endif
