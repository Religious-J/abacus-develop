#include "ions.h"
#include "../src_pw/forces.h"
#include "../src_pw/stress_pw.h"
#include "../src_pw/global.h" // use chr.
#include "../src_pw/vdwd2.h"
#include "../src_pw/vdwd3.h"
#include "../src_pw/vdwd2_parameters.h"
#include "../src_pw/vdwd3_parameters.h"
#include "../src_pw/pw_complement.h"
#include "../src_pw/pw_basis.h"
#include "variable_cell.h" // mohan add 2021-02-01

void Ions::opt_ions_pw(void)
{
	TITLE("Ions","opt_ions_pw");
	timer::tick("Ions","opt_ions_pw");
	
	if(GlobalV::OUT_LEVEL=="i")
	{
		cout << setprecision(12);
    	cout<< " " << setw(7)<< "ISTEP" 
		<<setw(5)<< "NE"
		<<setw(15)<< "ETOT(eV)"
		<<setw(15)<< "EDIFF(eV)"
        <<setw(15)<< "MAX_F(eV/A)"
        <<setw(15)<< "TRADIUS(Bohr)"
		<<setw(8)<< "UPDATE"
		<<setw(11)<< "ETIME(MIN)"
		<<setw(11)<< "FTIME(MIN)"
        <<endl;
	}

	// allocation for ion movement.	
	if(GlobalV::FORCE)
	{
		IMM.allocate();
		CE.allocate_ions();
	}

	if(GlobalV::STRESS)                    // pengfei Li 2018-05-14
	{
		LCM.allocate();
	}

    this->istep = 1;
	int force_step = 1;           // pengfei Li 2018-05-14
	int stress_step = 1;
	bool stop= false;
	
    while(istep <= GlobalV::NSTEP && !stop)
    {
		time_t estart = time(NULL);

		if(GlobalV::OUT_LEVEL=="ie")
		{
	
        	cout << " -------------------------------------------" << endl;
			if(GlobalV::CALCULATION=="relax") //pengfei 2014-10-13
			{
        		cout << " STEP OF ION RELAXATION : " << istep << endl;
			}
            else if(GlobalV::CALCULATION=="cell-relax")
            {
                cout << " RELAX CELL : " << stress_step << endl;
                cout << " RELAX IONS : " << force_step << " (in total: " << istep << ")" << endl;
                cout << " ---------------------------------------------------------" << endl;
            }
			else if(GlobalV::CALCULATION=="scf") //add 4 lines 2015-09-06, xiaohui
			{
        			cout << " SELF-CONSISTENT : " << endl;
			}
			else if(GlobalV::CALCULATION=="md")
			{
        		cout << " STEP OF MOLECULAR DYNAMICS : " << istep << endl;
			}
        	cout << " -------------------------------------------" << endl;

        	GlobalV::ofs_running << " -------------------------------------------" << endl;
			if(GlobalV::CALCULATION=="relax")
			{
        		GlobalV::ofs_running << " STEP OF ION RELAXATION : " << istep << endl;
			}
            else if(GlobalV::CALCULATION=="cell-relax")
            {
                GlobalV::ofs_running << " RELAX CELL : " << stress_step << endl;
                GlobalV::ofs_running << " RELAX IONS : " << force_step << " (in total: " << istep << ")" << endl;
                GlobalV::ofs_running << " ---------------------------------------------------------" << endl;
            }
			else if(GlobalV::CALCULATION=="md")
			{
        		GlobalV::ofs_running << " STEP OF MOLECULAR DYNAMICS : " << istep << endl;
			}
        	GlobalV::ofs_running << " -------------------------------------------" << endl;
		}

	//----------------------------------------------------------
    // about vdw, jiyy add vdwd3 and linpz add vdwd2
    //----------------------------------------------------------	
        if(INPUT.vdw_method=="d2")
        {
			// setup vdwd2 parameters
			vdwd2_para.initial_parameters(INPUT);
	        vdwd2_para.initset(ucell);
        }
        if(INPUT.vdw_method=="d3_0" || INPUT.vdw_method=="d3_bj")
        {
            vdwd3_para.initial_parameters(INPUT);
        }
		if(vdwd2_para.flag_vdwd2)		//Peize Lin add 2014-04-03, update 2021-03-09
		{
			Vdwd2 vdwd2(ucell,vdwd2_para);
			vdwd2.cal_energy();
			GlobalC::en.evdw = vdwd2.get_energy();
		}
		if(vdwd3_para.flag_vdwd3)		//jiyy add 2019-05-18, update 2021-05-02
		{
			Vdwd3 vdwd3(ucell,vdwd3_para);
			vdwd3.cal_energy();
			GlobalC::en.evdw = vdwd3.get_energy();
		}


		// mohan added eiter to count for the electron iteration number, 2021-01-28
		int eiter=0;		
        if (GlobalV::CALCULATION=="scf" || GlobalV::CALCULATION=="md" || GlobalV::CALCULATION=="relax" || GlobalV::CALCULATION=="cell-relax")  // pengfei 2014-10-13
        {
#ifdef __LCAO
			if( Exx_Global::Hybrid_Type::No==exx_global.info.hybrid_type  )
			{	
#endif		
				elec.self_consistent(istep-1);
				eiter = elec.iter;
#ifdef __LCAO
			}
			else if( Exx_Global::Hybrid_Type::Generate_Matrix == exx_global.info.hybrid_type )
			{
				throw invalid_argument(TO_STRING(__FILE__)+TO_STRING(__LINE__));
			}
			else	// Peize Lin add 2019-03-09
			{
				if( exx_global.info.separate_loop )
				{
					for( size_t hybrid_step=0; hybrid_step!=exx_global.info.hybrid_step; ++hybrid_step )
					{
						elec.self_consistent(istep-1);
						eiter += elec.iter;
						if( elec.iter==1 || hybrid_step==exx_global.info.hybrid_step-1 )		// exx converge
							break;
						exx_global.info.set_xcfunc(xcf);							
						exx_lip.cal_exx();
					}						
				}
				else
				{
					elec.self_consistent(istep-1);	
					eiter += elec.iter;
					exx_global.info.set_xcfunc(xcf);
					elec.self_consistent(istep-1);
					eiter += elec.iter;
				}
			}
#endif
        }
        else if(GlobalV::CALCULATION=="nscf")
        {
            elec.non_self_consistent(istep-1);
			eiter = elec.iter;
        }
		// mohan added 2021-01-28, perform stochastic calculations
		else if(GlobalV::CALCULATION=="scf-sto" || GlobalV::CALCULATION=="relax-sto" || GlobalV::CALCULATION=="md-sto")
		{
			elec_sto.scf_stochastic(istep-1);
			eiter = elec_sto.iter;
		}
	

		if(GlobalV::CALCULATION=="relax"|| GlobalV::CALCULATION=="md" || GlobalV::CALCULATION=="cell-relax")
		{
			CE.update_all_pos(ucell);
		}

		if(pot.out_potential == 2)
		{
			stringstream ssp;
			stringstream ssp_ave;
			ssp << GlobalV::global_out_dir << "ElecStaticPot";
			ssp_ave << GlobalV::global_out_dir << "ElecStaticPot_AVE";
			pot.write_elecstat_pot(ssp.str(), ssp_ave.str()); //output 'Hartree + local pseudopot'
		}

		time_t eend = time(NULL);
		time_t fstart = time(NULL);


        if (GlobalV::CALCULATION=="scf" || GlobalV::CALCULATION=="relax" || GlobalV::CALCULATION=="cell-relax")
        {
			stop = this->force_stress(istep, force_step, stress_step);    // pengfei Li 2018-05-14
		}
		time_t fend = time(NULL);


		if(GlobalV::OUT_LEVEL=="i")
		{
			double etime_min = difftime(eend, estart)/60.0; 
			double ftime_min = difftime(fend, fstart)/60.0; 
			stringstream ss;
			ss << GlobalV::MOVE_IONS << istep;
			
			cout << " " << setw(7) << ss.str() 
			<< setw(5) << eiter 
			<< setw(15) << setprecision(6) << GlobalC::en.etot * Ry_to_eV 
			<< setw(15) << IMM.get_ediff() * Ry_to_eV
			<< setprecision(3)
			<< setw(15) << IMM.get_largest_grad() * Ry_to_eV / 0.529177
			<< setw(15) << IMM.get_trust_radius()
			<< setw(8) << IMM.get_update_iter()
			<< setprecision(2) << setw(11) << etime_min
			<< setw(11) << ftime_min << endl;
		}

		++istep;

    }

    if(GlobalV::CALCULATION=="scf" || GlobalV::CALCULATION=="relax" || GlobalV::CALCULATION=="cell-relax")
    {
        GlobalV::ofs_running << "\n\n --------------------------------------------" << endl;
        GlobalV::ofs_running << setprecision(16);
        GlobalV::ofs_running << " !FINAL_ETOT_IS " << GlobalC::en.etot * Ry_to_eV << " eV" << endl; 
        GlobalV::ofs_running << " --------------------------------------------\n\n" << endl;
    }


	if(GlobalV::OUT_LEVEL=="i")
	{
		cout << " ION DYNAMICS FINISHED :)" << endl;
	}

	timer::tick("Ions","opt_ions_pw");
    return;
}


bool Ions::force_stress(const int &istep, int &force_step, int &stress_step)  // pengfei Li 2018-05-14
{
	TITLE("Ions","force_stress");

	if(!GlobalV::FORCE && !GlobalV::STRESS)
	{
		return 1;
	}


	if(GlobalV::FORCE&&!GlobalV::STRESS)
	{
		// (1) calculate the force.
		matrix force;
		Forces fcs;
		fcs.init(force);

		// (2) move the ions.
		bool converged = false;
		//if(GlobalV::CALCULATION=="md") //mohan add
		//{
		//	md.init_md(istep, fcs.force);
		//}
		//else	
		//{
			//IMM.cal_movement(istep, fcs.force, GlobalC::en.etot);
		//}
		if(GlobalV::CALCULATION=="relax")
		{
			IMM.cal_movement(istep, istep, force, GlobalC::en.etot);
			converged = IMM.get_converged();

            if(converged || (istep==GlobalV::NSTEP) ) 
            {
                return 1;
            }
            else
            {
                GlobalV::ofs_running << " Setup the structure factor in plane wave basis." << endl;
                GlobalC::pw.setup_structure_factor();

                GlobalV::ofs_running << " Setup the extrapolated charge." << endl;
                // charge extrapolation if istep>0.
                CE.extrapolate_charge();
			
                GlobalV::ofs_running << " Setup the Vl+Vh+Vxc according to new structure factor and new charge." << endl;
                // calculate the new potential accordint to
                // the new charge density.
                pot.init_pot( istep, GlobalC::pw.strucFac );

                GlobalV::ofs_running << " Setup the new wave functions?" << endl;
                GlobalC::wf.wfcinit();
            }
        }
        else
        {
            return 1;
        }
    }

	static bool converged_force = false;              // pengfe Li  2018-05-14
	static bool converged_stress = false;

	if(!GlobalV::FORCE&&GlobalV::STRESS)
	{
		Stress_PW ss;
		matrix stress;
		ss.cal_stress(stress);

		double unit_transform = 0.0;
		unit_transform = RYDBERG_SI / pow(BOHR_RADIUS_SI,3) * 1.0e-8;
		double external_stress[3] = {GlobalV::PRESS1,GlobalV::PRESS2,GlobalV::PRESS3};
		for(int i=0;i<3;i++)
		{
			stress(i,i) -= external_stress[i]/unit_transform;
		}
		GlobalV::PRESSURE = (stress(0,0)+stress(1,1)+stress(2,2))/3;
		if(GlobalV::CALCULATION=="cell-relax")
		{
			LCM.cal_lattice_change(stress_step, stress, GlobalC::en.etot);
			converged_stress = LCM.get_converged();
            //cout <<"converged_stress = "<<converged_stress<<endl;
            if(converged_stress)
            {
                return 1;
            }
            else
            {
                Variable_Cell::init_after_vc();
                pot.init_pot(stress_step, GlobalC::pw.strucFac); //LiuXh add 20180619
                GlobalV::ofs_running << " Setup the new wave functions?" << endl; //LiuXh add 20180619
                GlobalC::wf.wfcinit(); //LiuXh add 20180619
                ++stress_step;
                return 0;
            }
        }
        else
        {
            return 1;
        }
    }

	if(GlobalV::FORCE&&GlobalV::STRESS)
	{
		//cout<<" istep  force_step  stress_step  converged_force  converged_stress = "<<istep<<"  "<<force_step<<"  "<<stress_step<<"  "<<converged_force<<"  "<<converged_stress<<endl;

		matrix force;
		Forces fcs;
		fcs.init(force);
                matrix stress;
                Stress_PW ss;
                ss.cal_stress(stress);
		//IMM.cal_movement(force_step, fcs.force, GlobalC::en.etot);
        if(GlobalV::CALCULATION=="relax" || GlobalV::CALCULATION=="cell-relax")
        {
            IMM.cal_movement(istep, force_step, force, GlobalC::en.etot);
            converged_force = IMM.get_converged();

            //cout<<"converged_force = "<<converged_force<<endl;
            if(converged_force)
            {
                force_step = 1;


                double unit_transform = 0.0;
                unit_transform = RYDBERG_SI / pow(BOHR_RADIUS_SI,3) * 1.0e-8;
                double external_stress[3] = {GlobalV::PRESS1,GlobalV::PRESS2,GlobalV::PRESS3};
                for(int i=0;i<3;i++)
                {
                    stress(i,i) -= external_stress[i]/unit_transform;
                }
				GlobalV::PRESSURE = (stress(0,0)+stress(1,1)+stress(2,2))/3;

                if(GlobalV::CALCULATION=="cell-relax")
                {
                    LCM.cal_lattice_change(stress_step, stress, GlobalC::en.etot);
                    converged_stress = LCM.get_converged();
                    //cout <<"converged_stress = "<<converged_stress<<endl;
                    if(converged_stress)
                    {
                        return 1;
                    }
                    else
                    {
                        Variable_Cell::init_after_vc();
                        pot.init_pot(stress_step, GlobalC::pw.strucFac); //LiuXh add 20180619

                        GlobalV::ofs_running << " Setup the new wave functions?" << endl; //LiuXh add 20180619
                        GlobalC::wf.wfcinit(); //LiuXh add 20180619

                        ++stress_step;
                        return 0;
                    }
                }
                else
                {
                    return 1;
                }
            }
            else
            {
				GlobalV::ofs_running << " Setup the structure factor in plane wave basis." << endl;
                GlobalC::pw.setup_structure_factor();
				GlobalV::ofs_running << " Setup the extrapolated charge." << endl;
                CE.save_pos_next(ucell);
                CE.update_istep(force_step);
                CE.extrapolate_charge();
				GlobalV::ofs_running << " Setup the Vl+Vh+Vxc according to new structure factor and new charge." << endl;
                pot.init_pot( istep, GlobalC::pw.strucFac );
				GlobalV::ofs_running << " Setup the new wave functions?" << endl;
                GlobalC::wf.wfcinit();
                ++force_step;
                return 0;
            }
        }
        else
        {

            double unit_transform = 0.0;
            unit_transform = RYDBERG_SI / pow(BOHR_RADIUS_SI,3) * 1.0e-8;
            double external_stress[3] = {GlobalV::PRESS1,GlobalV::PRESS2,GlobalV::PRESS3};
            for(int i=0;i<3;i++)
            {
                stress(i,i) -= external_stress[i]/unit_transform;
            }
			GlobalV::PRESSURE = (stress(0,0)+stress(1,1)+stress(2,2))/3;
            return 1;
        }
    }

    return 0;
}


