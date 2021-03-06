//
//  slam_plotter.cpp
//  slam
//
//  Created by Roshan Shariff on 12-01-27.
//  Copyright (c) 2012 University of Alberta. All rights reserved.
//

#include <cmath>
#include <cstdio>
#include <cassert>
#include <sstream>
#include <iomanip>

#include <boost/range/adaptor/map.hpp>

#include "simulator/slam_plotter.hpp"
#include "planar_robot/rms_error.hpp"
#include "utility/bitree.hpp"
#include "utility/flat_map.hpp"
#include "utility/utility.hpp"


void slam_plotter::set_ground_truth (std::shared_ptr<slam_result_type> source) {
    ground_truth = source;
}

void slam_plotter::add_data_source (std::shared_ptr<slam_result_type> source, bool autoscale_map,
                                    std::string trajectory_title, std::string landmark_title,
                                    std::string feature_point_style, std::string trajectory_line_style,
                                    std::string state_arrow_style)
{
    data_sources.push_back ({
        source, autoscale_map, trajectory_title, landmark_title,
        feature_point_style, trajectory_line_style, state_arrow_style
    });
}


void slam_plotter::timestep (slam::timestep_type t) {
    if (output_dir) {
        std::ostringstream output_filename;
        output_filename << std::setfill('0') << std::setw(6) << std::size_t(t) << ".png";
        boost::filesystem::path output_file = (*output_dir)/output_filename.str();
        std::fprintf (gnuplot.handle(), "set output '%s'\n", output_file.c_str());
    }
    plot(t);
}


void slam_plotter::completed () {
    if (output_dir) {
        boost::filesystem::path output_file = (*output_dir)/"final.png";
        std::fprintf (gnuplot.handle(), "set output '%s'\n", output_file.c_str());
    }
    plot();
}


void slam_plotter::plot (boost::optional<slam::timestep_type> timestep) {
    
    if (!title.empty()) std::fprintf (gnuplot.handle(), "set title '%s'\n", title.c_str());
    else gnuplot.puts ("set title\n");
    
    //gnuplot.puts ("set key on inside center bottom horizontal Left reverse\n");
    gnuplot.puts ("set key on inside left top vertical Left reverse\n");
    gnuplot.puts ("set size ratio -1\n");
    gnuplot.puts ("set auto fix\n");
    gnuplot.puts ("set offsets graph 0.2, graph 0.05, graph 0.05, graph 0.05\n");

    for (const auto& source : data_sources) {
        const auto t = timestep ? *timestep : source.source->current_timestep();
        pose origin;
        if (ground_truth && source.source != ground_truth) {
            if (match_ground_truth && source.source->get_feature_map().size() >= 2) {
                origin = planar_robot::estimate_initial_pose(ground_truth->get_feature_map(),
                                                             source.source->get_feature_map());
            }
            else {
                origin = ground_truth->get_initial_state() + (-source.source->get_initial_state());
            }
        }
        plot_map (source, origin);
        plot_trajectory (source, t, origin);
        plot_state (source, t, origin);
    }
    gnuplot.plot ();
    
    if (output_dir) gnuplot.puts ("set output\n");
}


void slam_plotter::add_title (const std::string& title) {
    if (!title.empty()) std::fprintf (gnuplot.handle(), "title '%s' ", title.c_str());
    else gnuplot.puts ("notitle ");
}


void slam_plotter::plot_map (const data_source& source, const pose& origin) {
    
    using namespace boost::adaptors;
    
    const auto& feature_map = source.source->get_feature_map();
    if (feature_map.empty()) return;
    
    for (const auto& feature : values(feature_map)) {
        position pos = origin + feature;
        gnuplot << pos.x() << pos.y();
    }

    gnuplot.plot (2);    
    if (!source.autoscale_map) gnuplot.puts ("noautoscale ");
    add_title (source.landmark_title);
    gnuplot.puts ("with points ");
    gnuplot.puts (source.feature_point_style.c_str());
}


void slam_plotter::plot_trajectory (const data_source& source, slam::timestep_type t,
                                    const pose& origin) {
    
    for (slam::timestep_type i; i <= t; ++i) {
        pose state = origin + source.source->get_state(i);
        gnuplot << state.x() << state.y();
    }
    
    gnuplot.plot (2);
    gnuplot.puts ("noautoscale notitle with lines ");
    gnuplot.puts (source.trajectory_line_style.c_str());
}


void slam_plotter::plot_state (const data_source& source, slam::timestep_type t,
                               const pose& origin) {

    pose state = origin + source.source->get_state(t);
    
    double epsilon = 1;
    double xdelta = epsilon * std::cos (state.bearing());
    double ydelta = epsilon * std::sin (state.bearing());
    gnuplot << state.x() << state.y() << xdelta << ydelta;
    
    gnuplot.plot (4);
    gnuplot.puts ("noautoscale with vectors ");
    add_title (source.trajectory_title);
    gnuplot.puts (source.state_arrow_style.c_str());
}


boost::program_options::options_description slam_plotter::program_options () {
    namespace po = boost::program_options;
    po::options_description options ("SLAM Plotting Options");
    options.add_options()
    ("slam-plot-title", po::value<std::string>()->default_value("Simultaneous Localization and Mapping"),
     "Plot title")
    ("slam-plot-output-dir", po::value<std::string>(),
     "Output directory for plots (displayed on screen if unset)")
    ("slam-plot-isometry", "calculate best fit between estimated map and ground truth")
    ("debug-slam-plot", "switch to debugging mode");
    return options;
}


slam_plotter::slam_plotter (boost::program_options::variables_map& options)
: title (options["slam-plot-title"].as<std::string>()),
gnuplot(options.count("debug-slam-plot") != 0),
match_ground_truth(options.count("slam-plot-isometry"))
{
    if (options.count ("slam-plot-output-dir")) {
        output_dir = boost::filesystem::path (options["slam-plot-output-dir"].as<std::string>());
        boost::filesystem::create_directories (*output_dir);
        gnuplot.puts ("set terminal pngcairo font 'Sans,8' size 640, 480\n");
    }
}

