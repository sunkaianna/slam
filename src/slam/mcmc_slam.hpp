#ifndef _SLAM_MCMC_SLAM_HPP
#define _SLAM_MCMC_SLAM_HPP

#include <vector>
#include <map>
#include <cassert>
#include <cmath>
#include <utility>
#include <algorithm>

#include <boost/shared_ptr.hpp>
#include <boost/program_options.hpp>

#include "slam/interfaces.hpp"
#include "slam/slam_data.hpp"
#include "utility/random.hpp"
#include "utility/bitree.hpp"
#include "utility/flat_map.hpp"
#include "utility/utility.hpp"


namespace slam {
    
    /** This class implements the MCMC SLAM algorithm. To use it, construct an instance passing in
     a reference to a slam_data object and a random generator. This class will register to listen
     to slam data events. Call mcmc_slam::update to perform the specified number of MCMC-SLAM
     iterations on the data available so far. */
    
    template <class ControlModel, class ObservationModel>
    class mcmc_slam : public slam_result_of<ControlModel, ObservationModel> {

        using slam_data_type = slam_data<ControlModel, ObservationModel>;
        
        /** The types of labels for state and feature edges, respectively. State edge labels must
         form a group, with the binary + operator and unary - to form inverses. Additionally, this
         group must act on feature_type by the operation state + feature. */

        using state_type = typename ControlModel::result_type;
        using feature_type = typename ObservationModel::result_type;
        
        /** For each observed feature, store a pointer to the feature's observations, the time
         step relative to which the feature estimate is stored, and the estimate itself. */

        struct feature_estimate {
            const typename slam_data_type::feature_data_type* data;
            timestep_type parent_timestep;
            feature_type estimate;
        };
        
        /** Descriptions of the state and feature edges to be used with update (EdgeType&&) */
        
        struct state_edge {
            
            const timestep_type timestep;
            
            const ControlModel& distribution;
            typename utility::bitree<state_type>::reference estimate;
            utility::bitree<double>::reference weight;
            
            state_edge (mcmc_slam& mcmc, timestep_type t)
            : timestep   (t),
            distribution (mcmc.data->control(t)),
            estimate     (mcmc.state_estimates[t]),
            weight       (mcmc.state_weights[t])
            { }
        };
        
        struct feature_edge {
            
            const feature_estimate& feature;
            
            const ObservationModel& distribution;
            feature_type& estimate;
            utility::bitree<double>::reference weight;
            
            feature_edge (mcmc_slam& mcmc, std::size_t i)
            : feature    (mcmc.feature_estimates[i]),
            distribution (feature.data->at(feature.parent_timestep)),
            estimate     (mcmc.feature_estimates[i].estimate),
            weight       (mcmc.feature_weights[i])
            { }
        };
        
        
        /** Private data members */
        
        const boost::shared_ptr<const slam_data_type> data;
        boost::shared_ptr<slam_result<state_type, feature_type>> initialiser;
        
        random_source random;
        
        typename utility::bitree<state_type> state_estimates;
        utility::bitree<double> state_weights;
        
        std::vector<feature_estimate> feature_estimates;
        utility::bitree<double> feature_weights;
        
        // Map feature id to index in feature_estimates
        std::map<featureid_type, std::size_t> feature_index;
        
        // Cache of map estimate in the form required by get_feature_map()
        mutable utility::flat_map<featureid_type, feature_type> map_estimate;

        const double state_dim, feature_dim;
        unsigned int mcmc_updates_per_step = 1;
        
        // The next value expected by timestep
        timestep_type next_timestep;
        
        // Log likelihood of the current trajectory and map estimate
        double log_likelihood = 0;
        
        
        /** Private member functions */
        
        void add_state_edge ();
        void add_feature_edge (const typename slam_data_type::observation_data_type&);
        
        bool update ();
        template <class EdgeType> bool update (EdgeType&&, bool use_edge_weight);
        
        double edge_log_likelihood_ratio (const state_edge&, const state_type&) const;
        double edge_log_likelihood_ratio (const feature_edge&, const feature_type&) const;
        
        template <class Iter>
        double obs_likelihood_ratio (const feature_estimate&, const Iter begin, const Iter end,
                                     timestep_type obs_timestep, feature_type new_obs) const;
        
        bool initialiser_available (timestep_type t) const {
            return initialiser && (initialiser->timestep(t), true);
        }
                
        double edge_log_weight (double edge_log_likelihood, double edge_dim) const {
            return std::log(edge_dim) - edge_log_likelihood/edge_dim;
        }
        
        double edge_log_weight (double edge_log_likelihood, const state_edge&) {
            return edge_log_weight (edge_log_likelihood, state_dim);
        }
        
        double edge_log_weight (double edge_log_likelihood, const feature_edge&) {
            return edge_log_weight (edge_log_likelihood, feature_dim);
        }
        
    public:
        
        mcmc_slam (const mcmc_slam&) = delete;
        mcmc_slam& operator= (const mcmc_slam&) = delete;
        
        mcmc_slam (boost::shared_ptr<const slam_data_type>, boost::program_options::variables_map& options, unsigned int seed);
        
        static boost::program_options::options_description program_options ();
        
        double get_log_likelihood () const { return log_likelihood; }
        
        void set_initialiser (const decltype(initialiser)& init) { initialiser = init; }
        
        // Overridden virtual member functions of slam::slam_result
        
        virtual void timestep (timestep_type) override;        
        
        virtual timestep_type current_timestep () const override {
            return timestep_type(state_estimates.size());
        }
        
        virtual state_type get_state (timestep_type t) const override {
            assert (t <= current_timestep());
            return state_estimates.accumulate(t);
        }
        
        virtual feature_type get_feature (featureid_type id) const override {
            const feature_estimate& f = feature_estimates[feature_index.at(id)];
            return get_state(f.parent_timestep) + f.estimate;
        }
        
        virtual const decltype(state_estimates)& get_trajectory () const override {
            return state_estimates;
        }
        
        virtual const decltype(map_estimate)& get_feature_map () const override;
        
    };
    
} // namespace slam


template <class ControlModel, class ObservationModel>
void slam::mcmc_slam<ControlModel, ObservationModel>
::add_state_edge () {
    
    const ControlModel& control = data->control (current_timestep());
    
    const state_type estimate =
    initialiser_available (current_timestep()+1)
    ? -initialiser->get_state(current_timestep()) + initialiser->get_state(current_timestep()+1)
    : control.mean();
    
    const double edge_log_likelihood = control.log_likelihood (estimate);
    const double edge_weight = std::exp (edge_log_weight (edge_log_likelihood, state_dim));
    
    state_estimates.push_back (estimate);
    state_weights.push_back (edge_weight);
    log_likelihood += edge_log_likelihood;
    
    assert (state_estimates.size() == state_weights.size());
}


template <class ControlModel, class ObservationModel>
void slam::mcmc_slam<ControlModel, ObservationModel>
::add_feature_edge (const typename slam_data_type::observation_data_type& obs) {
    
    const feature_estimate f = {
        &obs.feature_data(),
        current_timestep(),
        initialiser_available (current_timestep())
        ? -initialiser->get_state(current_timestep()) + initialiser->get_feature(obs.feature_id())
        : obs.observation().mean()
    };

    const double edge_log_likelihood = obs.observation().log_likelihood (f.estimate);
    const double edge_weight = std::exp (edge_log_weight (edge_log_likelihood, feature_dim));
    
    // TODO: use emplace_back when C++11 standard library is available
    feature_estimates.push_back (f);
    feature_weights.push_back (edge_weight);
    log_likelihood += edge_log_likelihood;
    
    assert (feature_estimates.size() == feature_weights.size());
}


template <class ControlModel, class ObservationModel>
void slam::mcmc_slam<ControlModel, ObservationModel>
::timestep (const timestep_type timestep) {
    
    assert (timestep <= data->current_timestep());
    
    unsigned int mcmc_updates = 0;
    
    while (next_timestep <= timestep) {
        
        if (next_timestep > 0) add_state_edge();
        
        for (auto range = data->observations_at(next_timestep); range.first != range.second; ++range.first) {
            
            const auto& obs = range.first->second;
            // TODO: use emplace when C++11 standard library is available
            auto insertion = feature_index.insert (std::make_pair(obs.feature_id(), feature_estimates.size()));
            
            if (insertion.second) {
                add_feature_edge (obs);
                map_estimate.clear();
            }
            else {
                const feature_estimate& f = feature_estimates[insertion.first->second];
                log_likelihood += obs.observation().log_likelihood
                (state_estimates.accumulate(next_timestep, f.parent_timestep) + f.estimate);
            }
        }
        
        assert (next_timestep == current_timestep());
        ++next_timestep;
        
        mcmc_updates += mcmc_updates_per_step;
    }
    
    while (mcmc_updates--) update();
}


// Performs the MCMC SLAM update step
template <class ControlModel, class ObservationModel>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::update () -> bool {
    
    const double state_weight = state_weights.accumulate();
    const double feature_weight = feature_weights.accumulate();
    
    if (state_weight == 0 && feature_weight == 0) return false;
    
    //std::cout << "state: " << state_weight << ", feature: " << feature_weight << '\n';
    
    if ((state_weight+feature_weight) * random.uniform() < state_weight) {
        timestep_type timestep;
        while (timestep >= current_timestep()) {
            timestep = timestep_type (state_weights.binary_search (state_weight*random.uniform()));
        }
        bool accepted = update (state_edge (*this, timestep), true);
        //if (accepted) std::cout << "state change\n";
        return accepted;
    }
    else {
        std::size_t index;
        while (index >= feature_estimates.size()) {
            index = feature_weights.binary_search (feature_weight*random.uniform());
        }
        bool accepted = update (feature_edge (*this, index), true);
        //if (accepted) std::cout << "feature change\n";
        return accepted;
    }
}


template <class ControlModel, class ObservationModel>
template <class EdgeType>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::update (EdgeType&& edge, bool use_edge_weight) -> bool {
    
    const auto proposed = edge.distribution(random);
    const double log_ratio = edge_log_likelihood_ratio (edge, proposed);
    
    const double old_log_likelihood = edge.distribution.log_likelihood (edge.estimate);
    const double new_log_likelihood = edge.distribution.log_likelihood (proposed);

    const double new_log_weight = edge_log_weight (new_log_likelihood, edge);
    const double new_weight = std::exp (new_log_weight);
    
    bool accept;
    
    if (use_edge_weight) {
        const double old_log_weight = edge_log_weight (old_log_likelihood, edge);
        const double old_weight = std::exp (old_log_weight);
        const double weight_sum = state_weights.accumulate() + feature_weights.accumulate();
        const double normaliser = 1 + (new_weight - old_weight)/weight_sum;
        accept = normaliser*random.uniform() < std::exp (log_ratio + new_log_weight - old_log_weight);
    }
    else {
        accept = random.uniform() < std::exp (log_ratio);
    }
    
    if (accept) {
        edge.estimate = proposed;
        edge.weight = new_weight;
        log_likelihood += log_ratio - old_log_likelihood + new_log_likelihood;
        map_estimate.clear();
    }
    
    return accept;
}


/** Computes the log probability of all the edges whose labels change when the action edge given by
 action_id is updated. Changing an action splits the spanning tree of the inference graph into
 two subtrees, T1 and T2. T1 is the tree that contains action 0. A feature vertex lies in T1 if
 its parent action is before the one being changed, and it lies in T2 otherwise. If a feature vertex
 lies in T1, then the observation edges affected by the change are those after the change. Otherwise,
 if a feature vertex lies in T2 then the observations made before the change are affected. */
template <class ControlModel, class ObservationModel>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::edge_log_likelihood_ratio (const state_edge& edge, const state_type& proposed) const -> double {
    
    double log_ratio = 0.0; // Initialise log probability to zero.
    
    for (const auto& f : feature_estimates) { // iterate over all observed features.
        
        auto middle = f.data->upper_bound (edge.timestep);

        // Check whether the feature is in T2, and if so consider states before t. Otherwise
        // consider states after t.
        
        log_ratio += edge.timestep < f.parent_timestep

        ? obs_likelihood_ratio (f, f.data->begin(), middle, edge.timestep,
                                proposed
                                + state_estimates.accumulate (edge.timestep+1, f.parent_timestep)
                                + f.estimate)

        : obs_likelihood_ratio (f, middle, f.data->end(), edge.timestep+1,
                                -proposed
                                + state_estimates.accumulate (edge.timestep, f.parent_timestep)
                                + f.estimate);
    }
    
    return log_ratio;
}


template <class ControlModel, class ObservationModel>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::edge_log_likelihood_ratio (const feature_edge& edge, const feature_type& proposed) const -> double {
    
    return obs_likelihood_ratio (edge.feature, edge.feature.data->begin(), edge.feature.data->end(),
                                 edge.feature.parent_timestep, proposed);
}


template <class ControlModel, class ObservationModel>
template <class Iter>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::obs_likelihood_ratio (const feature_estimate& feature, const Iter begin, const Iter end,
                        timestep_type obs_timestep, feature_type new_obs) const -> double {
    
    double log_ratio = 0.0;
    
    feature_type old_obs = feature.estimate;
    old_obs = state_estimates.accumulate (obs_timestep, feature.parent_timestep) + old_obs;
    
    for (Iter iter = begin; iter != end; ++iter) {
        
        if (iter->first == feature.parent_timestep) continue;
        
        const state_type state_change = state_estimates.accumulate (iter->first, obs_timestep);
        new_obs = state_change + new_obs;
        old_obs = state_change + old_obs;

        obs_timestep = iter->first;
        const ObservationModel& obs_model = iter->second;
        log_ratio += obs_model.log_likelihood (new_obs) - obs_model.log_likelihood (old_obs);
    }
    
    return log_ratio;
}


template <class ControlModel, class ObservationModel>
auto slam::mcmc_slam<ControlModel, ObservationModel>
:: get_feature_map () const -> const decltype(map_estimate)& {
    
    if (map_estimate.size() != feature_estimates.size()) {
        
        map_estimate.clear();
        map_estimate.reserve (feature_estimates.size());
        
        for (const auto& id_index : feature_index) {
            const feature_estimate& f = feature_estimates[id_index.second];
            map_estimate.emplace_hint (map_estimate.end(),
                                       id_index.first, get_state(f.parent_timestep) + f.estimate);
        }
    }
    
    assert (map_estimate.size() == feature_estimates.size());
    return map_estimate;
}


template <class ControlModel, class ObservationModel>
auto slam::mcmc_slam<ControlModel, ObservationModel>
::program_options () -> boost::program_options::options_description {
    namespace po = boost::program_options;
    po::options_description options ("MCMC-SLAM Parameters");
    options.add_options()
    ("mcmc-steps", po::value<unsigned int>()->default_value(1), "MCMC iterations per simulation step")
    ("control-edge-importance", po::value<double>()->default_value(ControlModel::vector_dim), "degrees of freedom for control edges")
    ("observation-edge-importance", po::value<double>()->default_value(ObservationModel::vector_dim), "degrees of freedom for observation edges")
    ("mcmc-slam-seed", po::value<unsigned int>(), "MCMC-SLAM random seed");
    return options;
}


template <class ControlModel, class ObservationModel>
slam::mcmc_slam<ControlModel, ObservationModel>
::mcmc_slam (boost::shared_ptr<const slam_data<ControlModel, ObservationModel>> data,
             boost::program_options::variables_map& options, unsigned int seed)
: data      (data),
random      (remember_option (options, "mcmc-slam-seed", seed)),
state_dim   (options["control-edge-importance"].as<double>()),
feature_dim (options["observation-edge-importance"].as<double>())
{
    mcmc_updates_per_step = options["mcmc-steps"].as<unsigned int>();
}


#endif //_SLAM_MCMC_SLAM_HPP
