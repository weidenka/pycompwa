// Copyright (c) 2017 The ComPWA Team.
// This file is part of the ComPWA framework, check
// https://github.com/ComPWA/ComPWA/license.txt for details.

#include <pybind11/iostream.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "Core/Event.hpp"
#include "Core/Generator.hpp"
#include "Core/Kinematics.hpp"
#include "Core/Particle.hpp"
#include "Core/Random.hpp"
#include "Data/DataSet.hpp"
#include "Data/EvtGen/EvtGenGenerator.hpp"
#include "Data/Generate.hpp"
#include "Data/Root/RootDataIO.hpp"
#include "Data/Root/RootGenerator.hpp"
#include "Estimator/MinLogLH/MinLogLH.hpp"
#include "Optimizer/Minuit2/MinuitIF.hpp"

#include "Core/FunctionTree/FunctionTreeIntensity.hpp"
#include "Physics/BuilderXML.hpp"
#include "Physics/HelicityFormalism/HelicityKinematics.hpp"
#include "Physics/ParticleStateTransitionKinematicsInfo.hpp"

#include "Tools/FitFractions.hpp"
#include "Tools/Plotting/RootPlotData.hpp"
#include "Tools/UpdatePTreeParameter.hpp"

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(ComPWA::ParticleList);
PYBIND11_MAKE_OPAQUE(std::vector<ComPWA::Particle>);
PYBIND11_MAKE_OPAQUE(std::vector<ComPWA::Event>);
PYBIND11_MAKE_OPAQUE(std::vector<ComPWA::DataPoint>);
PYBIND11_DECLARE_HOLDER_TYPE(T, std::shared_ptr<T>);

PYBIND11_MODULE(ui, m) {
  m.doc() = "pycompwa module\n"
            "---------------\n";
  
  // -----------------------------------------
  //      Interface to Core components
  // -----------------------------------------
  
  /// Reinitialize the logger with level INFO and disabled log file.
  ComPWA::Logging("INFO");
  py::class_<ComPWA::Logging, std::shared_ptr<ComPWA::Logging>>(m, "Logging")
      .def(py::init<std::string, std::string>(), "Initialize logging system",
           py::arg("log_level"), py::arg("filename") = "")
      .def_property("level", &ComPWA::Logging::getLogLevel,
                    &ComPWA::Logging::setLogLevel);
  
  /// Write message to ComPWA logging system.
  m.def("log", [](std::string msg) { LOG(INFO) << msg; },
        "Write string to logging system.");

  /// Redirect ComPWA log output within a python scope.
  ///
  /// \code{.py}
  /// import pycompwa.ui as pwa
  /// with pwa.log_redirect(stdout=True, stderr=True):
  ///     // all logging to printed via python
  /// \endcode
  py::add_ostream_redirect(m, "log_redirect");

  /// Redirecting stdout and stderr to python printing system.
  /// This can not be changed during runtime.
  auto redirectors = std::make_unique<
      std::pair<py::scoped_ostream_redirect, py::scoped_estream_redirect>>();
  m.attr("_ostream_redirectors") = py::capsule(redirectors.release(),
  [](void *p) { delete reinterpret_cast<typename decltype(redirectors)::pointer>(p); });

  // ------- Parameters

  py::class_<ComPWA::FitParameter<double>>(m, "FitParameter")
      .def("__repr__",
           [](const ComPWA::FitParameter<double> &x) {
             std::stringstream ss;
             ss << x;
             return ss.str();
           })
      .def_readwrite("is_fixed", &ComPWA::FitParameter<double>::IsFixed)
      .def_readwrite("value", &ComPWA::FitParameter<double>::Value)
      .def_readwrite("name", &ComPWA::FitParameter<double>::Name)
      .def_readwrite("error", &ComPWA::FitParameter<double>::Error)
      .def_readwrite("bounds", &ComPWA::FitParameter<double>::Bounds);
  m.def("log", [](const ComPWA::FitParameter<double> p) { LOG(INFO) << p; });

  py::class_<ComPWA::FitParameterList>(m, "FitParameterList");

  m.def("log",
        [](const ComPWA::FitParameterList &list) {
          for (auto x : list)
            LOG(INFO) << x;
        },
        "Print FitParameter list to logging system.");

  // ------- Parameters in ptree
  m.def(
      "update_parameter_range_by_type",
      ComPWA::Tools::updateParameterRangeByType,
      "Update parameters' range of a ptree by parameter type, e.g., Magnitude.",
      py::arg("tree"), py::arg("parameter_type"), py::arg("min"),
      py::arg("max"));
  m.def("update_parameter_range_by_name",
        ComPWA::Tools::updateParameterRangeByName,
        "Update parameters' range of a ptree by parameter name.",
        py::arg("tree"), py::arg("parameter_name"), py::arg("min"),
        py::arg("max"));
  m.def("update_parameter_value", ComPWA::Tools::updateParameterValue,
        "Update parameters' value of a ptree by parameter name.",
        py::arg("tree"), py::arg("parameter_name"), py::arg("value"));
  m.def("fix_parameter", ComPWA::Tools::fixParameter,
        "Fix parameters current value (to value) of a ptree by parameter name.",
        py::arg("tree"), py::arg("parameter_name"), py::arg("value") = -999);
  m.def(
      "release_parameter", ComPWA::Tools::releaseParameter,
      "Release parameters' value (to new value) of a ptree by parameter name.",
      py::arg("tree"), py::arg("parameter_name"), py::arg("value") = -999);
  m.def("update_parameter",
        (void (*)(boost::property_tree::ptree &, const std::string &,
                  const std::string &, double, bool, double, double, bool, bool,
                  bool)) &
            ComPWA::Tools::updateParameter,
        "Update parameters' value, range, fix status, of a ptree.",
        py::arg("tree"), py::arg("key_type"), py::arg("key_value"),
        py::arg("value"), py::arg("fix"), py::arg("min"), py::arg("max"),
        py::arg("update_value"), py::arg("update_fix"),
        py::arg("update_range"));
  m.def("update_parameter",
        (void (*)(boost::property_tree::ptree &,
                  const ComPWA::FitParameterList &)) &
            ComPWA::Tools::updateParameter,
        "Update parameters according input FitParameters.", py::arg("tree"),
        py::arg("fit_parameters"));

  // ------- Data

  py::class_<ComPWA::Particle>(m, "Particle")
      .def(py::init<std::array<double, 4>, int>(), "", py::arg("p4"),
           py::arg("pid"))
      .def("__repr__",
           [](const ComPWA::Particle &p) {
             std::stringstream ss;
             ss << p;
             return ss.str();
           })
      .def("p4", [](const ComPWA::Particle &p) { return p.fourMomentum()(); });

  py::bind_vector<std::vector<ComPWA::Particle>>(m, "ParticleList");
  py::class_<ComPWA::Event>(m, "Event")
      .def(py::init<>())
      .def("particle_list",
           [](const ComPWA::Event &ev) { return ev.ParticleList; })
      .def("weight", [](const ComPWA::Event &ev) { return ev.Weight; });

  py::bind_vector<std::vector<ComPWA::Event>>(m, "EventList");

  py::class_<ComPWA::Data::Root::RootDataIO,
             std::shared_ptr<ComPWA::Data::Root::RootDataIO>>(m, "RootDataIO")
      .def(py::init<const std::string &, int>())
      .def(py::init<const std::string &>())
      .def(py::init<>())
      .def("readData", &ComPWA::Data::Root::RootDataIO::readData,
           "Read ROOT tree from file.", py::arg("input_file"))
      .def("writeData", &ComPWA::Data::Root::RootDataIO::writeData,
           "Save data as ROOT tree to file.", py::arg("data"), py::arg("file"));

  m.def("log", [](const ComPWA::DataPoint p) { LOG(INFO) << p; });

  py::class_<ComPWA::Data::DataSet>(m, "DataSet")
      .def_readonly("data", &ComPWA::Data::DataSet::Data)
      .def_readonly("weights", &ComPWA::Data::DataSet::Weights)
      .def_readonly("variable_names", &ComPWA::Data::DataSet::VariableNames);

  m.def(
      "convert_events_to_dataset",
      [](const std::vector<ComPWA::Event> evts, const ComPWA::Kinematics &kin) {
        return ComPWA::Data::convertEventsToDataSet(evts, kin);
      },
      "Internally convert the events to data points.", py::arg("events"),
      py::arg("kinematics"));
  m.def("add_intensity_weights", &ComPWA::Data::addIntensityWeights,
        "Add the intensity values as weights to this data sample.",
        py::arg("intensity"), py::arg("events"), py::arg("kinematics"));

  // ------- Particles
  py::class_<ComPWA::ParticleList>(m, "PartList")
      .def(py::init<>())
      .def("__repr__", [](const ComPWA::ParticleList &p) {
        std::stringstream ss;
        ss << p;
        return ss.str();
      });

  m.def("read_particles",
        [](std::string filename) { return ComPWA::readParticles(filename); },
        "Read particles from a xml file.", py::arg("xml_filename"));

  m.def("insert_particles",
        [](ComPWA::ParticleList &partlist, std::string filename) {
          ComPWA::insertParticles(partlist, filename);
        },
        "Insert particles to a list from a xml file. Already defined particles "
        "will be overwritten!");

  // ------- Kinematics

  py::class_<ComPWA::Kinematics, std::shared_ptr<ComPWA::Kinematics>>(
      m, "Kinematics")
      .def("convert", &ComPWA::Kinematics::convert,
           "Convert event to DataPoint.")
      .def("get_kinematic_variable_names",
           &ComPWA::Kinematics::getKinematicVariableNames)
      .def("phsp_volume", &ComPWA::Kinematics::phspVolume,
           "Convert event to DataPoint.");

  py::class_<ComPWA::Physics::ParticleStateTransitionKinematicsInfo>(
      m, "ParticleStateTransitionKinematicsInfo");

  py::class_<
      ComPWA::Physics::HelicityFormalism::HelicityKinematics,
      ComPWA::Kinematics,
      std::shared_ptr<ComPWA::Physics::HelicityFormalism::HelicityKinematics>>(
      m, "HelicityKinematics")
      .def(py::init<ComPWA::ParticleList, std::vector<ComPWA::pid>,
                    std::vector<ComPWA::pid>, std::array<double, 4>>())
      .def(py::init<ComPWA::ParticleList, std::vector<ComPWA::pid>,
                    std::vector<ComPWA::pid>>())
      .def(py::init<
           const ComPWA::Physics::ParticleStateTransitionKinematicsInfo &,
           double>())
      .def(py::init<
           const ComPWA::Physics::ParticleStateTransitionKinematicsInfo &>())
      .def("create_all_subsystems", &ComPWA::Physics::HelicityFormalism::
                                        HelicityKinematics::createAllSubsystems)
      .def("get_particle_state_transition_kinematics_info",
           &ComPWA::Physics::HelicityFormalism::HelicityKinematics::
               getParticleStateTransitionKinematicsInfo)
      .def("print_sub_systems",
           [](const ComPWA::Physics::HelicityFormalism::HelicityKinematics
                  &kin) {
             LOG(INFO) << "Subsystems used by HelicityKinematics:";
             for (auto i : kin.subSystems()) {
               // Have to add " " here (bug in boost 1.59)
               LOG(INFO) << " " << i;
             }
           });

  m.def("create_helicity_kinematics",
        [&](const std::string &filename, ComPWA::ParticleList partL) {
          boost::property_tree::ptree pt;
          boost::property_tree::xml_parser::read_xml(filename, pt);
          auto it = pt.find("HelicityKinematics");
          if (it != pt.not_found()) {
            return ComPWA::Physics::createHelicityKinematics(partL, it->second);
          } else {
            throw ComPWA::BadConfig(
                "pycompwa::create_helicity_kinematics(): "
                "HelicityKinematics tag not found in xml file!");
          }
        },
        "Create a helicity kinematics from a xml file. The file "
        "should contain a kinematics section.",
        py::arg("xml_filename"), py::arg("particle_list"));

  // ------- Intensity

  py::class_<ComPWA::Intensity, std::shared_ptr<ComPWA::Intensity>>(
      m, "Intensity");

  py::class_<ComPWA::FunctionTree::FunctionTreeIntensity, ComPWA::Intensity,
             std::shared_ptr<ComPWA::FunctionTree::FunctionTreeIntensity>>(
      m, "FunctionTreeIntensity")
      .def("evaluate", &ComPWA::FunctionTree::FunctionTreeIntensity::evaluate)
      .def("updateParametersFrom",
           [](ComPWA::FunctionTree::FunctionTreeIntensity &x,
              ComPWA::FitParameterList pars) {
             std::vector<double> params;
             for (auto x : pars)
               params.push_back(x.Value);
             x.updateParametersFrom(params);
           })
      .def("print", &ComPWA::FunctionTree::FunctionTreeIntensity::print,
           "print function tree");

  m.def(
      "create_intensity",
      [&](const std::string &filename, ComPWA::ParticleList partL,
          ComPWA::Kinematics &kin,
          const std::vector<ComPWA::Event> &PhspSample) {
        boost::property_tree::ptree pt;
        boost::property_tree::xml_parser::read_xml(filename, pt);
        auto it = pt.find("Intensity");
        if (it != pt.not_found()) {
          ComPWA::Physics::IntensityBuilderXML Builder(partL, kin, it->second,
                                                       PhspSample);
          return Builder.createIntensity();
        } else {
          throw ComPWA::BadConfig(
              "pycompwa::create_helicity_kinematics(): "
              "HelicityKinematics tag not found in xml file!");
        }
      },
      "Create an intensity and a helicity kinematics from a xml file. The file "
      "should contain a particle list, and a kinematics and intensity section.",
      py::arg("xml_filename"), py::arg("particle_list"), py::arg("kinematics"),
      py::arg("phsp_sample"));

  //------- Generate

  py::class_<ComPWA::UniformRealNumberGenerator>(m,
                                                 "UniformRealNumberGenerator");

  py::class_<ComPWA::StdUniformRealGenerator,
             ComPWA::UniformRealNumberGenerator>(m, "StdUniformRealGenerator")
      .def(py::init<int>());

  py::class_<ComPWA::Data::Root::RootUniformRealGenerator,
             ComPWA::UniformRealNumberGenerator>(m, "RootUniformRealGenerator")
      .def(py::init<int>());

  py::class_<ComPWA::PhaseSpaceEventGenerator>(m, "PhaseSpaceEventGenerator");

  py::class_<ComPWA::Data::Root::RootGenerator,
             ComPWA::PhaseSpaceEventGenerator>(m, "RootGenerator")
      .def(py::init<
           const ComPWA::Physics::ParticleStateTransitionKinematicsInfo &>());

  py::class_<ComPWA::Data::EvtGen::EvtGenGenerator,
             ComPWA::PhaseSpaceEventGenerator>(m, "EvtGenGenerator")
      .def(py::init<
           const ComPWA::Physics::ParticleStateTransitionKinematicsInfo &>());

  m.def("generate",
        [](unsigned int n, std::shared_ptr<ComPWA::Kinematics> kin,
           const ComPWA::PhaseSpaceEventGenerator &gen,
           std::shared_ptr<ComPWA::Intensity> intens,
           ComPWA::UniformRealNumberGenerator &randgen) {
          return ComPWA::Data::generate(n, *kin, gen, *intens, randgen);
        },
        "Generate sample from an Intensity", py::arg("size"), py::arg("kin"),
        py::arg("gen"), py::arg("intens"), py::arg("random_gen"));

  m.def("generate",
        [](unsigned int n, std::shared_ptr<ComPWA::Kinematics> kin,
           ComPWA::UniformRealNumberGenerator &randgen,
           std::shared_ptr<ComPWA::Intensity> intens,
           const std::vector<ComPWA::Event> &phspsample) {
          return ComPWA::Data::generate(n, *kin, randgen, *intens, phspsample);
        },
        "Generate sample from an Intensity, using a given phase space sample.",
        py::arg("size"), py::arg("kin"), py::arg("gen"), py::arg("intens"),
        py::arg("phspSample"));

  m.def("generate",
        [](unsigned int n, std::shared_ptr<ComPWA::Kinematics> kin,
           ComPWA::UniformRealNumberGenerator &randgen,
           std::shared_ptr<ComPWA::Intensity> intens,
           const std::vector<ComPWA::Event> &phspsample,
           const std::vector<ComPWA::Event> &toyphspsample) {
          return ComPWA::Data::generate(n, *kin, randgen, *intens, phspsample,
                                        toyphspsample);
        },
        "Generate sample from an Intensity. In case that detector "
        "reconstruction and selection is considered in the phase space sample "
        "a second pure toy sample needs to be passed.",
        py::arg("size"), py::arg("kin"), py::arg("gen"), py::arg("intens"),
        py::arg("phspSample"), py::arg("toyPhspSample") = nullptr);

  m.def("generate_phsp", &ComPWA::Data::generatePhsp,
        "Generate phase space sample");

  m.def("generate_importance_sampled_phsp",
        &ComPWA::Data::generateImportanceSampledPhsp,
        "Generate an Intensity importance weighted phase space sample",
        py::arg("size"), py::arg("kin"), py::arg("gen"), py::arg("intens"),
        py::arg("random_gen"));

  //------- Estimator + Optimizer

  py::class_<ComPWA::Estimator::Estimator<double>>(m, "Estimator");

  py::class_<ComPWA::FunctionTree::FunctionTreeEstimator,
             ComPWA::Estimator::Estimator<double>>(m, "FunctionTreeEstimator")
      .def("print", &ComPWA::FunctionTree::FunctionTreeEstimator::print,
           "print function tree");

  m.def("create_unbinned_log_likelihood_function_tree_estimator",
        (std::pair<ComPWA::FunctionTree::FunctionTreeEstimator,
                   ComPWA::FitParameterList>(*)(
            ComPWA::FunctionTree::FunctionTreeIntensity &,
            const ComPWA::Data::DataSet &)) &
            ComPWA::Estimator::createMinLogLHFunctionTreeEstimator,
        py::arg("intensity"), py::arg("datapoints"));

  py::class_<
      ComPWA::Optimizer::Optimizer<ComPWA::Optimizer::Minuit2::MinuitResult>>(
      m, "Optimizer");

  py::class_<
      ComPWA::Optimizer::Minuit2::MinuitIF,
      ComPWA::Optimizer::Optimizer<ComPWA::Optimizer::Minuit2::MinuitResult>>(
      m, "MinuitIF")
      .def(py::init<>())
      .def("optimize", &ComPWA::Optimizer::Minuit2::MinuitIF::optimize,
           "Start minimization.");

  //------- FitResult

  py::class_<ComPWA::FitResult>(m, "FitResult")
      .def_readonly("final_parameters", &ComPWA::FitResult::FinalParameters)
      .def_readonly("initial_parameters", &ComPWA::FitResult::InitialParameters)
      .def_readonly("initial_estimator_value",
                    &ComPWA::FitResult::InitialEstimatorValue)
      .def_readonly("final_estimator_value",
                    &ComPWA::FitResult::FinalEstimatorValue)
      .def_property_readonly(
          "fit_duration_in_seconds",
          [](const ComPWA::FitResult &x) { return x.FitDuration.count(); })
      .def_readonly("covariance_matrix", &ComPWA::FitResult::CovarianceMatrix);

  py::class_<ComPWA::Optimizer::Minuit2::MinuitResult, ComPWA::FitResult>(
      m, "MinuitResult")
      .def("log",
           [](const ComPWA::Optimizer::Minuit2::MinuitResult &Result) {
             LOG(INFO) << Result;
           },
           "Print fit result to the logging system.")
      .def("write",
           [](const ComPWA::Optimizer::Minuit2::MinuitResult &r,
              std::string file) {
             std::ofstream ofs(file);
             boost::archive::xml_oarchive oa(ofs);
             oa << BOOST_SERIALIZATION_NVP(r);
           },
           py::arg("file"));

  m.def("initializeWithFitResult", &ComPWA::initializeWithFitResult,
        "Initializes an Intensity with the parameters of a FitResult.",
        py::arg("intensity"), py::arg("fit_result"));

  /*m.def("fit_fractions", &ComPWA::Tools::calculateFitFractions,
        "Calculates the fit fractions for all components of a given coherent "
        "intensity.",
        py::arg("intensity"), py::arg("sample"),
        py::arg("components") = std::vector<std::string>());

  m.def(
      "fit_fractions_with_propagated_errors",
      [](std::shared_ptr<const ComPWA::Physics::CoherentIntensity> CohIntensity,
         std::shared_ptr<ComPWA::Data::DataSet> Sample,
         std::shared_ptr<ComPWA::Optimizer::Minuit2::MinuitResult> Result,
         const std::vector<std::string> &Components) {
        ComPWA::Tools::calculateFitFractionsWithCovarianceErrorPropagation(
            CohIntensity, Sample, Result->covarianceMatrix(), Components);
      },
      "Calculates the fit fractions and errors for all components of a given "
      "coherent intensity.",
      py::arg("intensity"), py::arg("sample"), py::arg("fit_result"),
      py::arg("components") = std::vector<std::string>());*/

  //------- Plotting

  m.def("create_data_array",
        [](ComPWA::Data::DataSet DataSample) {
          auto KinVarNames = DataSample.VariableNames;
          KinVarNames.push_back("weight");

          std::vector<std::vector<double>> DataArray(DataSample.Data);
          DataArray.push_back(DataSample.Weights);
          return std::make_pair(KinVarNames, DataArray);
        },
        py::return_value_policy::move);

  m.def("create_fitresult_array",
        [](std::shared_ptr<ComPWA::Intensity> Intensity,
           ComPWA::Data::DataSet DataSample) {
          auto KinVarNames = DataSample.VariableNames;
          KinVarNames.push_back("intensity");
          KinVarNames.push_back("weight");

          std::vector<std::vector<double>> DataArray(DataSample.Data);
          DataArray.push_back(DataSample.Weights);
          DataArray.push_back(Intensity->evaluate(DataSample.Data));
          return std::make_pair(KinVarNames, DataArray);
        },
        py::return_value_policy::move);

  m.def(
      "create_rootplotdata",
      [](const std::string &filename, std::shared_ptr<ComPWA::Kinematics> kin,
         const ComPWA::Data::DataSet &DataSample,
         const ComPWA::Data::DataSet &PhspSample,
         std::shared_ptr<ComPWA::Intensity> Intensity,
         std::map<std::string, std::shared_ptr<ComPWA::Intensity>>
             IntensityComponents,
         const ComPWA::Data::DataSet &HitAndMissSample,
         const std::string &option) {
        try {
          auto KinematicsInfo =
              (std::dynamic_pointer_cast<
                   ComPWA::Physics::HelicityFormalism::HelicityKinematics>(kin)
                   ->getParticleStateTransitionKinematicsInfo());
          ComPWA::Tools::Plotting::RootPlotData plotdata(KinematicsInfo,
                                                         filename, option);
          plotdata.writeData(DataSample);
          if (Intensity) {
            plotdata.writeIntensityWeightedPhspSample(
                PhspSample, *Intensity,
                std::string("intensity_weighted_phspdata"),
                IntensityComponents);
          }
          plotdata.writeHitMissSample(HitAndMissSample);
        } catch (const std::exception &e) {
          LOG(ERROR) << e.what();
        }
      },
      py::arg("filename"), py::arg("kinematics"), py::arg("data_sample"),
      py::arg("phsp_sample") = ComPWA::Data::DataSet(),
      py::arg("intensity") = std::shared_ptr<ComPWA::Intensity>(nullptr),
      py::arg("intensity_components") =
          std::map<std::string, std::shared_ptr<ComPWA::Intensity>>(),
      py::arg("hit_and_miss_sample") = ComPWA::Data::DataSet(),
      py::arg("tfile_option") = "RECREATE");
}
