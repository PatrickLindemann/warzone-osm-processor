#pragma once

#include <stdexcept>
#include <string>
#include <iostream>
#include <exception>
#include <algorithm>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/value_semantic.hpp>

#include "io/reader.hpp"
// #include "io/writer.hpp"
#include "functions/project.hpp"
#include "mapmaker/assembler.hpp"
#include "mapmaker/builder.hpp"
#include "mapmaker/compressor.hpp"
#include "mapmaker/connector.hpp"
#include "mapmaker/filter.hpp"
#include "mapmaker/inspector.hpp"
#include "mapmaker/projector.hpp"
#include "model/container.hpp"
#include "model/geometry/rectangle.hpp"
#include "util/validate.hpp"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace routine
{

    namespace create
    {
        
        /* Main function */

        void run(const std::vector<std::string>& args, char* argv[])
        {
            // Define constants
            const fs::path FILE_PATH = fs::system_complete(fs::path(argv[0]));

            // Define variables
            fs::path input;
            fs::path output;
            level_type territory_level;
            std::vector<level_type> bonus_levels;
            size_t width;
            size_t height;
            double compression_epsilon;
            double filter_epsilon;
            bool verbose;

            // Define the positional options
            po::positional_options_description positional;
            positional.add("input", -1);

            // Define the general options
            po::options_description options("Allowed options");
            options.add_options()
                ("input", po::value<fs::path>(&input),
                    "Sets the input file path.\nAllowed file formats: .osm, .pbf")
                ("output,o", po::value<fs::path>(&output),
                        "Sets the path prefix for the output files.")
                ("territory-level,t", po::value<level_type>(&territory_level)->default_value(0),
                    "Sets the admin_level of boundaries that will be be used as territories."
                    "\nInteger between 1 and 12.")
                ("bonus-levels,b", po::value<std::vector<level_type>>(&bonus_levels)->multitoken(),
                    "Sets the admin_level of boundaries that will be be used as bonus links."
                    "\nInteger between 1 and 12. If none are specified, no bonus links will be generated.")
                ("width,w", po::value<size_t>(&width)->default_value(1000),
                    "Sets the generated map width in pixels."
                    "\nIf set to 0, the width will be determined automatically.")
                ("height,h", po::value<size_t>(&height)->default_value(0),
                    "Sets the generated map height in pixels."
                    "\nIf set to 0, the height will be determined automatically.")
                ("compression-tolerance,c", po::value<double>(&compression_epsilon)->default_value(0.0),
                        "Sets the minimum distance tolerance for the compression algorithm."
                        "\nIf set to 0, no compression will be applied.")
                ("filter-tolerance,f", po::value<double>(&filter_epsilon)->default_value(0.0),
                        "Sets the surface area ratio tolerance for filtering boundaries."
                        "\nIf set to 0, no filter will be applied.")
                ("verbose", po::bool_switch(&verbose)->default_value(false), "Enables verbose logging.")
                ("help,h", "Shows this help message.");

            // Parse the specified arguments
            po::variables_map vm;
            po::store(po::command_line_parser(args)
                .options(options)
                .positional(positional)
                .run(), vm);
            po::notify(vm);

            // Validate the parsed variables. If a variable is invalid,
            // the exception will be passed to the executing instance.
            util::validate_file("input", input);
            util::validate_levels(territory_level, bonus_levels);
            util::validate_dimensions(width, height);
            util::validate_epsilon("compression-tolerance", compression_epsilon);
            util::validate_epsilon("filter-tolerance", filter_epsilon);

            // Set the output file naming prefix
            if (output.string() != "")
            {
                output = fs::path(output);
            }
            else
            {
                output = FILE_PATH.parent_path() / "../out" / (input.filename().replace_extension(""));
            }

            // Read the file info and print it to the console
            InfoContainer info = io::reader::get_info(input.string());
            info.print(std::cout);

            /* Read the file contents and extract the nodes, ways and relations */
            std::cout << "Reading file data from file \"" << input << "\"..." << std::endl;
            DataContainer data = io::reader::get_data(input.string(), territory_level, bonus_levels);
            if (!data.incomplete_relations.empty()) {
                std::cerr << "Warning! Some member ways missing for these multipolygon relations:";
                for (const auto id : data.incomplete_relations) {
                    std::cerr << " " << id;
                }
                std::cerr << "\n";
            }
            std::cout << "Finished file reading." << std::endl;

            // Compress the extracted ways
            if (compression_epsilon > 0)
            {
                std::cout << "Compressing ways... " << std::endl;
                size_t nodes_before = data.nodes.size();
                mapmaker::compressor::Compressor compressor{ data.nodes, data.ways };
                compressor.compress_ways(compression_epsilon);
                size_t nodes_after = data.nodes.size();
                std::cout << "Compressed ways successfully." << '\n'
                            << "Results: " << '\n'
                            << "  Nodes (before): " << std::to_string(nodes_before) << '\n'
                            << "  Nodes (after):  " << std::to_string(nodes_after) << std::endl;
            }

            // Assemble the territory areas
            std::cout << "Assembling territories from relations..." << std::endl;    
            mapmaker::assembler::SimpleAreaAssembler territory_assembler{ data.nodes, data.ways, data.relations };
            data.areas = territory_assembler.assemble_areas({ territory_level });
            std::cout << "Assembled " << data.areas.size() << " territories successfully." << std::endl;

            // Create the neighbor graph and component map
            std::cout << "Calculating territory relations (neighbors and components)..." << std::endl;
            mapmaker::inspector::NeighborInspector neighbor_inspector{ data.areas };
            auto [neighbors, components] = neighbor_inspector.get_relations();
            std::cout << "Calculated relations sucessfully. " << std::endl;

            // Apply the area filter on the territories
            if (filter_epsilon > 0)
            {
                std::cout << "Fitering territories by their relative size..." << std::endl;
                mapmaker::filter::AreaFilter territory_filter {
                    data.areas, data.relations, components, data.nodes, data.ways
                };
                size_t territories_before = data.areas.size();
                territory_filter.filter_areas(filter_epsilon);
                size_t territories_after = data.areas.size();
                std::cout << "Compressed territories successfully." << '\n'
                            << "Results: " << '\n'
                            << "  Territories (before): " << std::to_string(territories_before) << '\n'
                            << "  Territories (after):  " << std::to_string(territories_after) << std::endl;
            }
            
            // Assemble the bonus areas
            if (!bonus_levels.empty())
            {
                std::cout << "Assembling bonus areas from relations..." << std::endl;    
                mapmaker::assembler::ComplexAreaAssembler bonus_assembler{ data.nodes, data.ways, data.relations };
                size_t areas_before = data.areas.size();
                bonus_assembler.assemble_areas(data.areas, bonus_levels);
                size_t areas_after = data.areas.size();
                std::cout << "Assembled " << areas_after - areas_before << " bonus areas successfully." << std::endl;            
            }

            // Apply the map projections
            std::cout << "Applying the map projections... " << std::endl;
            mapmaker::projector::Projector<double> projector{ data.nodes };  
            // Convert the map coordinates to radians
            projector.apply_projection(functions::RadianProjection<double>{});
            // Apply the MercatorProjection
            projector.apply_projection(functions::MercatorProjection<double>{});
            std::cout << "Applied projections sucessfully on " << data.nodes.size() << " nodes." << std::endl;

            // Scale the map
            std::cout << "Scaling the map... " << std::endl;
            // TODO re-calculate bounds
            geometry::Rectangle<double> bounds;
            // Check if a dimension is set to auto and calculate its value
            // depending on the map bounds
            if (width == 0 || height == 0)
            {
                if (width == 0)
                {
                    width = bounds.width() / bounds.height() * height;
                }
                else
                {
                    height = bounds.height() / bounds.width() * width;
                }
            }
            // Apply the scaling projections
            // Scale the map according to the dimensions
            projector.apply_projection(functions::UnitProjection<double>{
                { bounds.min().x(), bounds.max().x() },
                { bounds.min().y(), bounds.max().y() }
            });
            projector.apply_projection(functions::IntervalProjection<double>{
                { 0.0, 1.0 }, { 0.0, 1.0 }, { 0.0, width }, { 0.0, height }
            });
            std::cout << "Scaled the map sucessfully. The output size will be " << width << "x" << height << "px" << std::endl;

            /*
            // Assemble the geometries
            std::cout << "Converting areas to geometries... " << std::endl;
            mapmaker::converter::GeometryConverter converter{ data.nodes, data.areas };
            converter.run();
            data.geometries = converter.geometries();
            std::cout << "Built geometries successfully. " << std::endl;


            // Calculate the centerpoints
            std::cout << "Calculating centerpoints... " << std::endl;
            std::cout << "Calculated centerpoints successfully. " << std::endl;

            // Build the final map
            std::cout << "Building the map... " << std::endl;
            mapmaker::builder::MapBuilder builder{ data };
            builder.run();
            map::Map map = builder.map();
            std::cout << "Built the map sucessfully." << std::endl;

            // Export the map data as .svg file
            std::cout << "Exporting map data..." << std::endl;
            std::string outpath_string = output_path.string();
            io::writer::write_metadata(outpath_string, map);
            std::cout << "Exported metadata to " << output_path << ".json" << std::endl;
            io::writer::write_map(outpath_string, map);
            std::cout << "Exported map to " << output_path << ".svg" << std::endl;
            io::writer::write_preview(outpath_string, map);
            std::cout << "Exported metadata to " << output_path << ".preview.svg" << std::endl;
            std::cout << "Data export finished successfully. " << std::endl;

            std::cout << "Finished Mapmaker after " << 0 << " seconds." << std::endl;
            */
        }

    }

}