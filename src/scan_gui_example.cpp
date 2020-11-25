/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

/**
 * @file scan_gui_example.cpp
 * @brief Example demonstrating how to read profile data from scan heads.
 *
 * This application shows the fundamentals of how to stream profile data
 * from scan heads up through the client API and into your own code. Each scan
 * head will be initially configured before scanning using generous settings
 * that should guarantee that valid profile data is obtained. Following
 * configuration, a limited number of profiles will be collected before halting
 * the scan and disconnecting from the scan heads.
 */
//#define MAHI_GUI_NO_CONSOLE
#define MAHI_GUI_USE_DISCRETE_GPU

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <joescan_pinchot.h>
#include <Mahi/Gui.hpp>
#include <Mahi/Util.hpp>
#include <implot.h>
#include "jsCircleHough.h"

using namespace mahi::gui;
using namespace mahi::util;

#define PI 3.14159265


// utility structure for realtime plot
struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    ImVector<ImVec2> Data;
    ScrollingBuffer() {
        MaxSize = 2000;
        Offset  = 0;
        Data.reserve(MaxSize);
    }
    void AddPoint(float x, float y) {
        if (Data.size() < MaxSize)
            Data.push_back(ImVec2(x,y));
        else {
            Data[Offset] = ImVec2(x,y);
            Offset =  (Offset + 1) % MaxSize;
        }
    }
    void Erase() {
        if (Data.size() > 0) {
            Data.shrink(0);
            Offset  = 0;
        }
    }
};

// Inherit from Application
class MyApp : public Application {
public:
  double x_data[2][JS_PROFILE_DATA_LEN];
  double y_data[2][JS_PROFILE_DATA_LEN];
  int data_length[2] = {0};
  double x_center[2] = {0};
  double y_center[2] = {0};
  double weight[2] = {0};
  double current_time = 0;
  ScrollingBuffer centerData[2];

  jsScanSystem scan_system = nullptr;
  std::vector<jsScanHead> scan_heads;
  jsProfile profile;
  jsCircleHough circle_hough;

  // 640x480 px window
  MyApp(std::vector<uint32_t> &serial_numbers) : Application() {
    int32_t r = 0;

    int32_t radius = 810;
    jsCircleHoughConstraints c;
    c.step_size = 50;
    c.x_lower = -15000;
    c.x_upper = 15000;
    c.y_lower = -30000;
    c.y_upper = 30000;

    circle_hough = jsCircleHoughCreate(radius, &c);
      
    ImGui::StyleColorsMahiDark3();
    try {
      // First step is to create a scan manager to manage the scan heads.
      scan_system = jsScanSystemCreate();
      if (nullptr == scan_system) {
        throw std::runtime_error("failed to create scan system");
      }

      // Create a scan head software object for each serial number passed in
      // through the command line. We'll assign each one a unique ID starting at
      // zero; we'll use this as an easy index for associating profile data with
      // a given scan head.
      int32_t id = 0;
      for (auto serial : serial_numbers) {
        auto scan_head = jsScanSystemCreateScanHead(scan_system, serial, id);
        if (nullptr == scan_head) {
          throw std::runtime_error("failed to create scan_head");
        }
        scan_heads.emplace_back(scan_head);
        id++;
      }

      // For this example application, we'll just use the same configuration
      // settings we made use of in the "Configure and Connect" example. The
      // only real difference here is that we will be applying this configuration
      // to multiple scan heads, using a "for" loop to configure each scan head
      // one after the other.
      jsScanHeadConfiguration config;
      config.scan_offset_us = 0;
      config.camera_exposure_time_min_us = 10000;
      config.camera_exposure_time_def_us = 47000;
      config.camera_exposure_time_max_us = 900000;
      config.laser_on_time_min_us = 100;
      config.laser_on_time_def_us = 100;
      config.laser_on_time_max_us = 1000;
      config.laser_detection_threshold = 120;
      config.saturation_threshold = 800;
      config.saturation_percentage = 30;

      for (auto scan_head : scan_heads) {
        r = jsScanHeadConfigure(scan_head, &config);
        if (0 > r) {
          throw std::runtime_error("failed to set scan head configuration");
        }

        // To illustrate that each scan head can be configured independently,
        // we'll alternate between two different windows for each scan head. The
        // other options we will leave the same only for the sake of convenience;
        // these can be independently configured as needed.
        uint32_t serial = jsScanHeadGetSerial(scan_head);
        uint32_t id = jsScanHeadGetId(scan_head);
        if (id % 2) {
          std::cout << serial << ": scan window is 20, -20, -20, 20" << std::endl;
          r = jsScanHeadSetWindowRectangular(scan_head, 20.0, -20.0, -20.0, 20.0);
        } else {
          std::cout << serial << ": scan window is 30, -30, -30, 30" << std::endl;
          r = jsScanHeadSetWindowRectangular(scan_head, 30.0, -30.0, -30.0, 30.0);
        }
        if (0 > r) {
          throw std::runtime_error("failed to set window");
        }

        r = jsScanHeadSetAlignment(scan_head, 0.0, 0.0, 0.0, false);
        if (0 > r) {
          throw std::runtime_error("failed to set alignment");
        }
      }

      // Now that the scan heads are configured, we'll connect to the heads.
      r = jsScanSystemConnect(scan_system, 10);
      if (0 > r) {
        // This error condition indicates that something wrong happened during
        // the connection process itself and should be understood by extension
        // that none of the scan heads are connected.
        throw std::runtime_error("failed to connect");
      } else if (jsScanSystemGetNumberScanHeads(scan_system) != r) {
        // On this error condition, connection was successful to some of the scan
        // heads in the system. We can query the scan heads to determine which
        // one successfully connected and which ones failed.
        for (auto scan_head : scan_heads) {
          if (false == jsScanHeadIsConnected(scan_head)) {
            uint32_t serial = jsScanHeadGetSerial(scan_head);
            std::cout << serial << " is NOT connected" << std::endl;
          }
        }
        throw std::runtime_error("failed to connect to all scan heads");
      }

      // Once configured, we can then read the maximum scan rate supported by
      // the scan system. This value depends on how all of the scan heads manged
      // by the scan system are configured.
      double max_scan_rate_hz = jsScanSystemGetMaxScanRate(scan_system);
      if (max_scan_rate_hz <= 0.0) {
        throw std::runtime_error("failed to read max scan rate");
      }
      std::cout << "max scan rate is " << max_scan_rate_hz << std::endl;

      // To begin scanning on all of the scan heads, all we need to do is
      // command the scan system to start scanning. This will cause all of the
      // scan heads associated with it to begin scanning at the specified rate
      // and data format.
      jsDataFormat data_format = JS_DATA_FORMAT_XY_FULL_LM_FULL;
      double scan_rate_hz = 200;
      std::cout << "start scanning" << std::endl;
      r = jsScanSystemStartScanning(scan_system, scan_rate_hz, data_format);
      if (0 > r) {
        throw std::runtime_error("failed to start scanning");
      }
    } catch (std::exception &e) {
      std::cout << "ERROR: " << e.what() << std::endl;

      // If we end up with an error from the API, we can get some additional
      // diagnostics by looking at the error code.
      if (0 > r) {
        const char *err_str = nullptr;
        jsGetError(r, &err_str);
        std::cout << "jsError (" << r << "): " << err_str << std::endl;
      }
    }
  }
    
  // Override update (called once per frame)
  void update() override {
    int32_t r = 0;
    bool stay_open;

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    // App logic and/or ImGui code goes here
    ImGui::Begin("Example", &stay_open, ImGuiWindowFlags_MenuBar);
    ImPlot::SetNextPlotLimits(-30.0, 30.0, -30.0, 30.0);
    if (ImPlot::BeginPlot("Profile Plot","X [inches]","Y [inches]",ImVec2(1200,800),ImPlotFlags_Equal)) {
      for (unsigned int j = 0; j < scan_heads.size(); j++) {
        auto profiles_available = jsScanHeadGetProfilesAvailable(scan_heads[j]);
        if(profiles_available < 0) {
            std::cout << "profiles availble fail?" << std::endl;
            throw std::runtime_error("profiles availble fail?");
        }
        
        if(profiles_available == 0) {
          std::cout << "No profiles available" << std::endl;
          continue;
        }
        
        if(profiles_available > 100) {
          std::cout << "Too many profiles available" << std::endl;
        }

        for(unsigned int k = 0; k < profiles_available; k++) {
          r = jsScanHeadGetProfiles(scan_heads[j], &profile, 1);
          if (0 > r) {
            std::cout << "failed to get profiles" << std::endl;
            throw std::runtime_error("failed to get profiles");
          }
          
          
          auto res = jsCircleHoughCalculate(circle_hough, &profile);
          x_center[profile.camera] = res.x / 1000.0;
          y_center[profile.camera] = res.y / 1000.0;
          weight[profile.camera] = res.weight;
          
          for (unsigned int idx = 0; idx < profile.data_len; idx++) {
            x_data[profile.camera][idx] = profile.data[idx].x / 1000.0;
            y_data[profile.camera][idx] = profile.data[idx].y / 1000.0;
            data_length[profile.camera] = profile.data_len;
          }

          current_time += ImGui::GetIO().DeltaTime;

          centerData[0].AddPoint(current_time, x_center[0]);
          centerData[1].AddPoint(current_time, y_center[0]);
        }

        if (data_length[0] > 0) {
          ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 1, ImVec4(0,1.0f,0,0.5f), IMPLOT_AUTO, ImVec4(0,1,0,1));
          ImPlot::PlotScatter("Camera 1", x_data[0], y_data[0], data_length[0]);
          ImPlot::Annotate(x_center[0],y_center[0],ImVec2(10,10),ImPlot::GetLastItemColor(),"Center");
        }

//        if (data_length[1] > 0) {
//          ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 1, ImVec4(0,0.75f,0,0.5f), IMPLOT_AUTO, ImVec4(0,0.5f,0,1));
//          ImPlot::PlotScatter("Camera 2", x_data[1], y_data[1], data_length[1]);
//        }
      }
      ImPlot::EndPlot();
    }

    ImPlot::SetNextPlotLimitsY(-30, 30);
    ImPlot::SetNextPlotLimitsX(current_time - 10.0, current_time, ImGuiCond_Always);
    if (ImPlot::BeginPlot("Center Location","Time [seconds]","X/Y [inches]",ImVec2(1200,200))) {
      if (centerData[0].Data.size() > 0) {
        ImPlot::PlotLine("X Center", &centerData[0].Data[0].x, &centerData[0].Data[0].y, centerData[0].Data.size(), centerData[0].Offset, 2 * sizeof(float));
        ImPlot::PlotLine("Y Center", &centerData[1].Data[0].x, &centerData[1].Data[0].y, centerData[1].Data.size(), centerData[1].Offset, 2 * sizeof(float));
      }
      ImPlot::EndPlot();
    }
    ImGui::End();
    
    if(!stay_open) {
      
      quit();
    }
  }
};

/**
 * @brief This function is a small utility function used to explore profile
 * data. In this case, it will iterate over the valid profile data and
 * find the highest measurement in the Y axis.
 *
 * @param profiles Array of profiles from a single scan head.
 * @param num_profiles Total number of profiles contained in array.
 * @return jsProfileData The profile measurement with the greatest Y axis value
 * from the array of profiles passed in.
 */
jsProfileData find_scan_profile_highest_point(jsProfile *profiles,
                                              uint32_t num_profiles)
{
  jsProfileData p = {0, 0, 0};
  

  for (unsigned int i = 0; i < num_profiles; i++) {
    for (unsigned int j = 0; j < profiles[i].data_len; j++) {
      if (profiles[i].data[j].y > p.y) {
        p.brightness = profiles[i].data[j].brightness;
        p.x = profiles[i].data[j].x;
        p.y = profiles[i].data[j].y;
      }
    }
  }

  return p;
}

int main(int argc, char *argv[])
{
  std::vector<uint32_t> serial_numbers;

  if (2 > argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL..." << std::endl;
    return 1;
  }

  // Grab the serial number(s) passed in through the command line.
  for (int i = 1; i < argc; i++) {
    serial_numbers.emplace_back(strtoul(argv[i], NULL, 0));
  }

  const char *version_str;
  jsGetAPIVersion(&version_str);
  std::cout << "joescanapi " << version_str << std::endl;

  MyApp app(serial_numbers);
  app.run();
  return 0;

}
