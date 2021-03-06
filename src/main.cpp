/*
 * main.cpp
 *
 *  Created on: Mar 27, 2015
 *      Author: Hannah_Pinson
 */

#include <time.h>
#include <iostream>
#include <sstream>
#include <fstream>

#include "simulation.h"
#include "physical_constants.h"
#include "output_methods.h"

using namespace std;




/*---------------- MAIN ------------------- */


int main(){


	/*! ------general simulation parameters------*/

	/*
	 * grid_scaling example:
	 *
	 *  0    0									0 0 0 0
	 *   			grid_scaling = 2 -> 		0 0 0 0
	 *         			    					0 0 0 0
	 *  0    0									0 0 0 0
	 */

	double grid_scaling = 1; //! - grid_scaling: accuracy of simulation for same real-world distance, time, frequency => delta_t/grid_scaling, delta_x/grid_scaling; #timesteps * grid_scaling, #nodes * grid_scaling
	double S_c = 1; //! - S_c: courant number = delta_t * c / delta_x
	double delta_t = 1*pow(10.0, -12)/grid_scaling; //! - delta_t: discrete unit of time, in seconds
	double delta_x = c*delta_t/S_c; //! - delta_x: discrete unit of space, in meter
	int zones = 8; //! - zones: number of zones in simulation, used for easy specification of positions
	int nodes_per_zone = 500 * grid_scaling; //! - nodes_per_zone: nodes per zone, used for changing the number of nodes without changing relative positions of materials and sources
	int dimension = zones*nodes_per_zone; //! - dimension: total number of spatial nodes



	/*! ------boundary: perfectly matched layers (PMLs)------*/

	double PML_eps = 1.5*eps0; //! -PML_eps : permittivity of the PML
	double PML_mu = 1.5*mu0; //! -PML_mu : permeability of the PML
	double PML_sigma_e = PML_eps*0.01/(delta_t*grid_scaling); //! -PML_sigma_e : electric conductivity of the PML
	double PML_sigma_h = PML_mu*0.01/(delta_t*grid_scaling); //! -PML_sigma_h : magnetic conductivity of the PML. Needed to make the PML impedance-matched, allowed since the PML is a non-physical region.
	int startPML_left = 0; //! -startPML_left : node where the left PML starts, usually node 0.
	int endPML_left = 2*nodes_per_zone;
	int startPML_right = dimension - 2*nodes_per_zone;
	int endPML_right = dimension;
	Static_Material PML_left(PML_sigma_e, PML_sigma_h, PML_eps, PML_mu, startPML_left, endPML_left);
	Static_Material PML_right(PML_sigma_e, PML_sigma_h, PML_eps, PML_mu, startPML_right, endPML_right);


	//static material objects (here: PMLs)
	vector<Static_Material> static_layers;
	static_layers.push_back(PML_left);
	static_layers.push_back(PML_right);



	/*! ------source------*/


	double initial_frequency = 2 * pow(10.0, 9); //! - initial_frequency: frequency of the source signal, in Hz
	double initial_wavelength = c/initial_frequency; //! - initial_wavelength: wavelength of the source signal, in m
	double N_lambda = initial_wavelength/delta_x; //! - N_lambda: number of nodes per wavelength (free space and source signal)
	double amplitude_factor = 1; //! - amplitude_factor: amplitude factor for the source signal. Multiply by 1/impedance for magnetic sources.
	double width = 1000 * grid_scaling;//2500;
	double delay = 2000 * grid_scaling;//5000;//2*width;
	int source_position = 3*nodes_per_zone; //! -source_position : node where the source is located
	char source_type = 'a';

	Gaussian_packet_functor gaussian_packet(S_c, N_lambda, delay, width, amplitude_factor);
	Source gaussian_source_e(source_position, source_type, &gaussian_packet);




	/*No_source_functor no_source_functor;
	Source no_source_h(source_position, source_type, &no_source_functor);*/

	Gaussian_packet_functor gaussian_packet_h(S_c, N_lambda, delay, width, amplitude_factor/imp0);
	Source gaussian_source_h(source_position, source_type, &gaussian_packet_h);



	/*! ------metasurface------*/

	//functors for time-varying conductivities

	int m_array[1] = {1};
	double a_array[3] = {0.6, 0.95, 1.50};
	double gamma_array[1] = {0.08};
	double beta_array[6] = {1.3 * 1./initial_frequency, 1.2 * 1./initial_frequency, 1.1 * 1./initial_frequency, 1 * 1./initial_frequency, 0.9  * 1./initial_frequency, 0.85 * 1./initial_frequency  };

	for (int i_beta = 4; i_beta < 5; i_beta++){
		for (int i_m = 1; i_m < 2; i_m++){
			for (int i_a = 0; i_a < 3; i_a++){
				for (int i_gamma = 0; i_gamma < 1; i_gamma++){

					double gamma = gamma_array[i_gamma];
					double a = a_array[i_a];
					double beta = beta_array[i_beta];
					double m = 1;

					string basename = "single_m_" + convert_to_string(i_m) + "_a_" + convert_to_string(i_a) + "_gamma_" + convert_to_string(i_gamma) + "_beta_" + convert_to_string(i_beta);

					Linear_Time_Variation t0_functor(gamma, beta); //can be used for multiple sheets

					double ksi_factor_e = 2/imp0/delta_x;
					double ksi_factor_h = 2*imp0/delta_x;
					Sigma_m_resonances sigma_e(&t0_functor, a, m, ksi_factor_e);
					Sigma_m_resonances sigma_h(&t0_functor, a, m, ksi_factor_h);

					vector<Dispersive_Metasurface> metasurfaces;
					int sheet1_position = source_position + 10 ;
					Dispersive_Metasurface sheet1(sheet1_position, sigma_e, sigma_h);
					metasurfaces.push_back(sheet1);


					int proxy_delay = sheet1_position + delay;
					Gaussian_packet_functor proxy_functor(S_c, N_lambda, proxy_delay, width, amplitude_factor);
					Source proxy_source(sheet1_position, source_type, &proxy_functor);

					/*! ------simulation objects and setup------*/

					//constructing free space simulation object & setup
					string free_space_name = "free_space";
					Simulation free_space(free_space_name, dimension, S_c, delta_t, "ABC", gaussian_source_e, gaussian_source_h, proxy_source, sheet1_position);
					free_space.static_setup(static_layers);

					//constructing metasurface simulation object & setup
					string name = "metasurface_" + basename;
					Simulation metasurface(name, dimension, S_c, delta_t, "ABC", gaussian_source_e, gaussian_source_h, proxy_source, sheet1_position);
					metasurface.static_setup(static_layers);
					metasurface.add_metasurfaces(metasurfaces);


					/*! ------output options------*/
					vector<int> sample_nodes;
					int readout_node1 = sheet1_position + 10 ;
					sample_nodes.push_back(readout_node1);

					vector<int> no_sample_nodes_h;

					int snapshotmodulo_electric = -1;//500*grid_scaling;
					int snapshotmodulo_magnetic = -1;//500*grid_scaling;
					string output_path = "/Users/Hannah_Pinson/Desktop/FDTD_metasurface_proper/clean_convolution/single_resonance_changed_sign/";


					//����� simulate
					try{ //try --> if error is 'thrown', rest of program skipped until 'catch' block catches the error

						/* ! consecutive simulations of the same Simulation object influence one another */

						//simulation
						cout << "starting simulation " << name << endl;
						clock_t start;
						start = clock();

						int timesteps = 10000 * grid_scaling / S_c ;
						metasurface.simulate(timesteps, output_path, snapshotmodulo_electric, snapshotmodulo_magnetic, sample_nodes, no_sample_nodes_h);
						//free_space.simulate(timesteps, output_path, snapshotmodulo_electric, snapshotmodulo_magnetic, sample_nodes, no_sample_nodes_h);


						double diff = ( clock() - start ) / (double) CLOCKS_PER_SEC;
						cout << "-----finished in "<< diff/60 << " minutes." << endl;

						ofstream parFile;
						parFile.open(output_path+"parameters.txt",std::fstream::trunc);
						parFile << " \\begin{tabular}{ l c } " << "\n";
						parFile << "$\\Delta t$ & $ " << delta_t << "$ \\" << "\n";
						parFile << "$\\Delta x$ & $" << delta_x << "$ \\" << "\n";
						parFile << "initial frequency & $ " << initial_frequency << " $ \\" << "\n";
						parFile << "source width & $" << width << "$\\" << "\n";
						parFile << "source delay & $" << delay << "$\\" << "\n";
						parFile << "source magnetic & $" << "on" << "$\\" << "\n";
						parFile << "m & $" << m << "$\\" << "\n";
						parFile << "a & $" << a << "$\\" << "\n";
						parFile << "gamma & $" << gamma << "$\\" << "\n";
						parFile << "beta & $" << beta << "$\\" << "\n";
						parFile << " \\end{tabular}" << "\n";

					}

					catch (string error_message){
						cerr << error_message << endl;
					}

				}
			}
		}
	}




	return 0;
}


