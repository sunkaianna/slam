#ifndef _SLAM_SLAM_DATA_HPP
#define _SLAM_SLAM_DATA_HPP

#include <vector>
#include <map>
#include <cassert>
#include <utility>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/utility.hpp>

/** This class stores a record of all state changes and observations, as probability distributions.
    It is also responsible for notifying listeners when new state changes and observations are added.
    ActionModel and ObservationModel are the types of distributions over state changes and
    observations, respectively. */
template <class ControlModel, class ObservationModel>
class slam_data : boost::noncopyable {

public:

	/** Use the native machine unsigned integral type to identify features and observations. This is 64
      bits on amd64 machines and 32 bits on i386 machines. */
	typedef size_t timestep_t;
	typedef size_t featureid_t;

    class listener : boost::noncopyable, public boost::enable_shared_from_this<listener> {
        
        boost::shared_ptr<const slam_data> data_ptr;
        
    public:
        
        void connect (boost::shared_ptr<const slam_data>);
        
        bool is_connected () const { return data_ptr.get() != 0; }
        
        const slam_data& data() const { assert(is_connected()); return *data_ptr; }
        
        void disconnect () { data_ptr.reset(); }
        
        virtual void add_control (timestep_t, const ControlModel&) { }
        
        virtual void add_observation (timestep_t, featureid_t, const ObservationModel&, bool new_feature) { }
        
        virtual void end_observation (timestep_t) { }
        
        virtual void end_simulation (timestep_t) { }
        
        virtual ~listener () { }
        
    };
        
	typedef boost::container::flat_map<timestep_t, ObservationModel> feature_data_type;

private:
    
    typedef std::map<featureid_t, feature_data_type> feature_map_type;

	std::vector<ControlModel> m_controls;
    feature_map_type m_features;

    mutable std::vector<boost::weak_ptr<listener> > m_listeners;
    
    template <class Functor> void foreach_listener (Functor);
    
public:
    
    typedef typename feature_map_type::const_iterator feature_iterator;
    
	timestep_t current_timestep () const {
        return m_controls.size();
    }

	/** Retrieve controls. */

	const ControlModel& control (timestep_t timestep) const {
        return m_controls.at(timestep);
    }
    
    /** Retrieve observations. */
    
    feature_iterator features_begin () const { return m_features.begin(); }
    feature_iterator features_end () const { return m_features.begin(); }
    
    feature_iterator find_feature (featureid_t feature_id) const {
        return m_features.find (feature_id);
    }

    const feature_data_type& feature_data (featureid_t feature_id) const {
        return m_features.at(feature_id);
    }
    
    const ObservationModel& feature_observation (featureid_t feature_id, timestep_t timestep) const {
        return m_features.at(feature_id).at(timestep);
    }
    
	/** Add new data */
    
	void add_control (const ControlModel&);

	void add_observation (featureid_t, const ObservationModel&);

	void end_observation () {
        foreach_listener (boost::bind (&listener::end_observation, _1, current_timestep()));
	}
    
    void end_simulation () {
        foreach_listener (boost::bind (&listener::end_simulation, _1, current_timestep()));
    }

};

template <class ControlModel, class ObservationModel>
void slam_data<ControlModel, ObservationModel>
::listener::connect (boost::shared_ptr<const slam_data> data_ptr) {
    this->data_ptr = data_ptr;
    data_ptr->m_listeners.push_back (this->shared_from_this());
}

template <class ControlModel, class ObservationModel>
template <class Functor>
void slam_data<ControlModel, ObservationModel>
::foreach_listener (Functor f) {
    typename std::vector<boost::weak_ptr<listener> >::iterator iter = m_listeners.begin();
    while (iter != m_listeners.end()) {
        if (boost::shared_ptr<listener> l = iter->lock()) {
            f (l.get());
            ++iter;
        }
        else {
            iter = m_listeners.erase (iter);
        }
    }
}

template <class ControlModel, class ObservationModel>
void slam_data<ControlModel, ObservationModel>
::add_control (const ControlModel& control) {
    timestep_t timestep = current_timestep();
    m_controls.push_back (control);
    foreach_listener (boost::bind (&listener::add_control, _1, timestep, boost::cref(control)));
}

template <class ControlModel, class ObservationModel>
void slam_data<ControlModel, ObservationModel>
::add_observation (const featureid_t feature_id, const ObservationModel& obs) {

    std::pair<typename feature_map_type::iterator, bool> feature_ins
    = m_features.insert (std::make_pair (feature_id, feature_data_type()));

    std::pair<typename feature_data_type::iterator, bool> obs_ins
    = feature_ins.first->second.insert (std::make_pair (current_timestep(), obs));
    
    if (!obs_ins.second) return;
    
    foreach_listener
    (boost::bind (&listener::add_observation, _1,
                  current_timestep(), feature_id, boost::cref(obs), feature_ins.second));
}


#endif //_SLAM_SLAM_DATA_HPP
