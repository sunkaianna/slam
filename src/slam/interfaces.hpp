//
//  interfaces.hpp
//  slam
//
//  Created by Roshan Shariff on 12-02-09.
//  Copyright (c) 2012 University of Alberta. All rights reserved.
//

#ifndef slam_interfaces_hpp
#define slam_interfaces_hpp

#include <cstddef>
#include <cmath>
#include <utility>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include "utility/container_fwd.hpp"

namespace slam {
    
    
    class timestep_type {
        std::size_t timestep;
    public:
        explicit timestep_type (std::size_t timestep = 0) : timestep(timestep) { }
        operator std::size_t () const { return timestep; }
        bool operator== (timestep_type other) const { return timestep == other.timestep; }
        bool operator< (timestep_type other) const { return timestep < other.timestep; }

        timestep_type& operator++ () { ++timestep; return *this; }
        timestep_type& operator-- () { --timestep; return *this; }
        timestep_type& operator+= (int x) { timestep += x; return *this; }
        timestep_type& operator-= (int x) { timestep -= x; return *this; }
    };
    
    using namespace std::rel_ops;
    
    inline timestep_type operator++ (timestep_type& t, int) { auto copy = t; ++t; return copy; }
    inline timestep_type operator-- (timestep_type& t, int) { auto copy = t; --t; return copy; }
    
    inline timestep_type operator+ (timestep_type t, int x) { return t += x; }
    inline timestep_type operator- (timestep_type t, int x) { return t -= x; }
    
    
    class featureid_type {
        std::size_t featureid;
    public:
        explicit featureid_type (std::size_t featureid) : featureid(featureid) { }
        operator std::size_t () const { return featureid; }
        bool operator== (featureid_type other) const { return featureid == other.featureid; }
        bool operator< (featureid_type other) const { return featureid < other.featureid; }
    };
    
    
    struct timestep_listener {
        virtual void timestep (timestep_type) = 0;
        virtual ~timestep_listener () = default;
    };
    
    
    struct data_source : public timestep_listener {
        virtual timestep_type current_timestep () const = 0;
    };
    
    
    template <class State, class Feature>
    struct slam_result : public data_source {
        
        using trajectory_type = utility::bitree<State>;
        using feature_map_type = utility::flat_map<featureid_type, Feature>;
        
        virtual State get_state (timestep_type) const = 0;
        virtual Feature get_feature (featureid_type) const = 0;

        virtual const trajectory_type& get_trajectory () const = 0;
        virtual const feature_map_type& get_feature_map () const = 0;
    };
    
    template <class ControlModel, class ObsModel>
    using slam_result_of = slam_result<typename ControlModel::result_type, typename ObsModel::result_type>;
    
    
    template <class Functor>
    auto make_timestep_listener (const Functor& functor) -> boost::shared_ptr<timestep_listener> {
        
        struct listener : public timestep_listener {
            Functor functor;
            listener (const Functor& functor) : functor(functor) { }
            virtual void timestep (timestep_type t) { this->functor(t); }
        };
        
        return boost::make_shared<listener>(functor);
    }
}

#endif