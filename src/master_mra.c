/* Copyright (c) 2014. BigHybrid Team. All rights reserved. */

/* This file is part of BigHybrid.

BigHybrid, MRSG and MRA++ are free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

BigHybrid, MRSG and MRA++ are distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with BigHybrid, MRSG and MRA++.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common_bighybrid.h"
#include "worker_mra.h"
#include "dfs_mra.h"


XBT_LOG_EXTERNAL_DEFAULT_CATEGORY (msg_test);

static FILE*       tasks_log;

static void print_mra_config (void);
static void print_mra_stats (void);
static int is_straggler_mra (enum mra_phase_e mra_phase, msg_host_t worker);
static int task_time_elapsed_mra (msg_task_t mra_task);
static void set_mra_speculative_tasks (enum mra_phase_e mra_phase, msg_host_t worker);
static void send_map_to_mra_worker (msg_host_t dest);
static void send_reduce_to_mra_worker (msg_host_t dest);
static void send_mra_task (enum mra_phase_e mra_phase, size_t tid, size_t data_src, msg_host_t dest);
static void finish_all_mra_task_copies (mra_task_info_t ti);

/** @brief  Main master function. */
int master_mra (int argc, char* argv[])
{
    mra_heartbeat_t  mra_heartbeat;
    msg_error_t  status;
    msg_host_t   worker;
    msg_task_t   msg = NULL;
    size_t       mra_wid;
    mra_task_info_t  ti;

    print_mra_config ();
    XBT_INFO ("JOB_MRA BEGIN"); XBT_INFO (" ");

    tasks_log = fopen ("tasks-mra.csv", "w");
    fprintf (tasks_log, "task_id,mra_phase,worker_id,time,action,shuffle_mra_end\n");

    while (job_mra.tasks_pending[MRA_MAP] + job_mra.tasks_pending[MRA_REDUCE] > 0)
    {
			msg = NULL;
			status = receive (&msg, MASTER_MRA_MAILBOX);
			if (status == MSG_OK)
				{
	    		worker = MSG_task_get_source (msg);
	    		mra_wid = get_mra_worker_id (worker);

	    		if (mra_message_is (msg, SMS_HEARTBEAT_MRA))
	     			{
							mra_heartbeat = &job_mra.mra_heartbeats[mra_wid];
	  
	    			if (is_straggler_mra (MRA_MAP, worker) )
	    				{
	    				  set_mra_speculative_tasks (MRA_MAP, worker);	
	    				 } 
	    			else 
	    			  {	   
	 							if (mra_heartbeat->slots_av[MRA_MAP] > 0 && dist_bruta [mra_wid] > 0) 
	    							send_map_to_mra_worker (worker); 
		    			 }
		    
		    		if (is_straggler_mra (MRA_REDUCE, worker) )
							{
							  set_mra_speculative_tasks (MRA_REDUCE, worker);}
							   		  
						else{
						
								if (mra_heartbeat->slots_av[MRA_REDUCE] > 0 && dist_bruta [mra_wid] > 0)     								
								   send_reduce_to_mra_worker (worker);
		    			}
		    						
	    			}
	    		else if (mra_message_is (msg, SMS_TASK_MRA_DONE))
	    			{
							ti = (mra_task_info_t) MSG_task_get_data (msg);

							if (job_mra.task_status[ti->mra_phase][ti->mra_tid] != T_STATUS_MRA_DONE)
								{
		    					job_mra.task_status[ti->mra_phase][ti->mra_tid] = T_STATUS_MRA_DONE;
		    					finish_all_mra_task_copies (ti);
		    					job_mra.tasks_pending[ti->mra_phase]--;
		    					if (job_mra.tasks_pending[ti->mra_phase] <= 0)
		    						{
											XBT_INFO (" ");
											XBT_INFO ("%s PHASE DONE", (ti->mra_phase==MRA_MAP?"MRA_MAP":"MRA_REDUCE"));
											XBT_INFO (" ");
		    						}
								}
							xbt_free_ref (&ti);
	    			}
	    	MSG_task_destroy (msg);
			}
  	}

    fclose (tasks_log);
 
    job_mra.finished = 1;      
    
    print_mra_config ();
    print_mra_stats ();
    XBT_INFO ("JOB END");

    return 0;
}

/** @brief  Print the job configuration. */
static void print_mra_config (void)
{
    XBT_INFO ("MRA_JOB CONFIGURATION:");
    XBT_INFO ("slots_mra: %d map, %d reduce", config_mra.mra_slots[MRA_MAP], config_mra.mra_slots[MRA_REDUCE]);
    XBT_INFO ("MRA_chunk replicas: %d", config_mra.mra_chunk_replicas);
    XBT_INFO ("MRA_chunk size: %.0f MB", config_mra.mra_chunk_size/1024/1024);
    XBT_INFO ("MRA_input chunks: %d", config_mra.mra_chunk_count);
    XBT_INFO ("MRA_input size: %d MB", config_mra.mra_chunk_count * (int)(config_mra.mra_chunk_size/1024/1024));
    XBT_INFO ("MRA_maps: %d", config_mra.amount_of_tasks_mra[MRA_MAP]);
    XBT_INFO ("MRA_reduces: %d", config_mra.amount_of_tasks_mra[MRA_REDUCE]);
    XBT_INFO ("grain factor: %d", config_mra.Fg);
    XBT_INFO ("MRA_map_output size: %.0f Bytes", (((config_mra.mra_chunk_size*config_mra.mra_perc/100)/config_mra.amount_of_tasks_mra[MRA_REDUCE])/config_mra.Fg));
    XBT_INFO ("MRA_workers: %d", config_mra.mra_number_of_workers);
    XBT_INFO ("MRA_grid power: %g flops", config_mra.grid_cpu_power);
    XBT_INFO ("MRA_average power: %g flops/s", config_mra.grid_average_speed);
    XBT_INFO ("MRA_heartbeat interval: %ds", config_mra.mra_heartbeat_interval);
    XBT_INFO (" ");
}

/** @brief  Print job statistics. */
static void print_mra_stats (void)
{
    XBT_INFO ("MRA_JOB STATISTICS:");
    XBT_INFO ("local maps: %d", stats_mra.map_local_mra);
    XBT_INFO ("non-local maps: %d", stats_mra.mra_map_remote);
    XBT_INFO ("speculative maps (local): %d", stats_mra.map_spec_mra_l);
    XBT_INFO ("speculative maps (remote): %d", stats_mra.map_spec_mra_r);
    XBT_INFO ("total non-local maps: %d", stats_mra.mra_map_remote + stats_mra.map_spec_mra_r);
    XBT_INFO ("total speculative maps: %d", stats_mra.map_spec_mra_l + stats_mra.map_spec_mra_r);
    XBT_INFO ("normal reduces: %d", stats_mra.reduce_mra_normal);
    XBT_INFO ("speculative reduces: %d", stats_mra.reduce_mra_spec);
    XBT_INFO (" ");
}

/* @brief  Checks if a worker is a straggler.
 * @param  worker  The worker to be probed.
 * @return 1 if true, 0 if false.
 */

static int is_straggler_mra (enum mra_phase_e mra_phase, msg_host_t worker)
{
    int     task_count;
    size_t  mra_wid;


    mra_wid = get_mra_worker_id (worker);

    task_count = (config_mra.mra_slots[MRA_MAP] + config_mra.mra_slots[MRA_REDUCE]) - (job_mra.mra_heartbeats[mra_wid].slots_av[MRA_MAP] + job_mra.mra_heartbeats[mra_wid].slots_av[MRA_REDUCE]);

  switch (mra_phase)
    {
	case MRA_MAP:
	 if (MSG_get_host_speed (worker) < avg_task_exec_map[mra_wid] && task_count > 0){
	   return 1;}
            break;
	    
	case MRA_REDUCE:
	  //if (MSG_get_host_speed (worker) < avg_task_exec_reduce[wid] && task_count > 0){
	  if (MSG_get_host_speed (worker) < config_mra.grid_average_speed && task_count > 0){
	   return 1;}
           break;
 
    }
     
     return 0;   
}



/* @brief  Returns for how long a task is running.
 * @param  mra_task  The task to be probed.
 * @return The amount of seconds since the beginning of the computation.
 */
static int task_time_elapsed_mra (msg_task_t mra_task)
{
    mra_task_info_t  ti;

    ti = (mra_task_info_t) MSG_task_get_data (mra_task);

    return (MSG_task_get_compute_duration (mra_task) - MSG_task_get_remaining_computation (mra_task))/ MSG_get_host_speed (config_mra.workers_mra[ti->mra_wid]);
}

/* @brief  Mark the tasks of a straggler as possible speculative tasks.
 * @param  worker  The straggler worker.
 */
 
//static void set_speculative_tasks (msg_host_t worker)

static void set_mra_speculative_tasks (enum mra_phase_e mra_phase, msg_host_t worker)
 {
    size_t       tid;
    size_t       mra_wid;
    mra_task_info_t  ti;

    mra_wid = get_mra_worker_id (worker);

      switch (mra_phase)
    {
	    case MRA_MAP:    
   				if (is_straggler_mra (MRA_MAP, worker) == 1 )
   					{
    					if (job_mra.mra_heartbeats[mra_wid].slots_av[MRA_MAP] < config_mra.mra_slots[MRA_MAP])
    						{
          				for (tid = 0; tid < config_mra.amount_of_tasks_mra[MRA_MAP]; tid++)
	    							{
	     								if (job_mra.task_list[MRA_MAP][tid][0] != NULL)
	       								{
		 											ti = (mra_task_info_t) MSG_task_get_data (job_mra.task_list[MRA_MAP][tid][0]);
		   										if (ti->mra_wid == mra_wid && task_time_elapsed_mra (job_mra.task_list[MRA_MAP][tid][0]) > 60)
		  											{
		    											job_mra.task_status[MRA_MAP][tid] = T_STATUS_MRA_TIP_SLOW;
		  											}
	       								}
	    							}
        				}
    				}
    	break;
  
       case MRA_REDUCE:
    			if (is_straggler_mra (MRA_REDUCE, worker) == 1 )
    				{
       				if (job_mra.mra_heartbeats[mra_wid].slots_av[MRA_REDUCE] < config_mra.mra_slots[MRA_REDUCE])
        				{
	   							for (tid = 0; tid < config_mra.amount_of_tasks_mra[MRA_REDUCE]; tid++)
	    							{
	      							if (job_mra.task_list[MRA_REDUCE][tid][0] != NULL)
	      								{
													ti = (mra_task_info_t) MSG_task_get_data (job_mra.task_list[MRA_REDUCE][tid][0]);
		  										if (ti->mra_wid == mra_wid && task_time_elapsed_mra (job_mra.task_list[MRA_REDUCE][tid][0]) > 60)
		  											{
		    											job_mra.task_status[MRA_REDUCE][tid] = T_STATUS_MRA_TIP_SLOW;
		  											}
	      								}
	    							}				
         				}
    				}
       break;
     } 
 } 

/**
 * @brief  Choose a map task, and send it to a worker.
 * @param  dest  The destination worker.
 */
static void send_map_to_mra_worker (msg_host_t dest)
{
    char*   flags;
    int     task_type;
    size_t  chunk;
    size_t  sid = NONE;
    size_t  tid = NONE;
    size_t  mra_wid;

    if (job_mra.tasks_pending[MRA_MAP] <= 0)
	  return;

    enum { LOCAL, REMOTE, LOCAL_SPEC, REMOTE_SPEC, NO_TASK };
    task_type = NO_TASK;

    mra_wid = get_mra_worker_id (dest);

    /* Look for a task for the worker. */
    for (chunk = 0; chunk < config_mra.mra_chunk_count; chunk++)
      {
	      if (job_mra.task_status[MRA_MAP][chunk] == T_STATUS_MRA_PENDING)
	       	{
	    		if (chunk_owner_mra[chunk][mra_wid])
	    		{
						task_type = LOCAL;
						tid = chunk;
						break;
	    		}
	    else
	    { //Altera o Comportamento do MAP e emissão de tarefas remotas
//		task_type = REMOTE;
//		tid = chunk;
	    }
	}
	else if (job_mra.task_status[MRA_MAP][chunk] == T_STATUS_MRA_TIP_SLOW
		&& task_type > REMOTE
		&& !job_mra.task_instances[MRA_MAP][chunk] )
	{
	    if (chunk_owner_mra[chunk][mra_wid])
	    {
		task_type = LOCAL_SPEC;
		tid = chunk;
	    }
	    else if (task_type > LOCAL_SPEC)
	    {
		task_type = REMOTE_SPEC;
		tid = chunk;
	    }
	}
    }

    switch (task_type)
    {
	case LOCAL:
	    flags = "";
	    sid = mra_wid;
	    stats_mra.map_local_mra++;
	    break;

	case REMOTE:
	    flags = "(non-local)";
	    sid = find_random_mra_chunk_owner (tid);
	    stats_mra.mra_map_remote++;
	    break;

	case LOCAL_SPEC:
	    flags = "(speculative)";
	    sid = mra_wid;
	    stats_mra.map_spec_mra_l++;
	    break;

	case REMOTE_SPEC:
	    flags = "(non-local, speculative)";
	    sid = find_random_mra_chunk_owner (tid);
	    stats_mra.map_spec_mra_r++;
	    break;

	default: return;
    }

    XBT_INFO ("MRA_map %zu assigned to %s %s", tid, MSG_host_get_name (dest), flags);

    send_mra_task (MRA_MAP, tid, sid, dest);
}

/**
 * @brief  Choose a reduce task, and send it to a worker.
 * @param  dest  The destination worker.
 */
static void send_reduce_to_mra_worker (msg_host_t dest)
{
    char*   flags;
    int     task_type;
    size_t  t;
    size_t  tid = NONE;
/* Hadoop code transfer initialize on 5% task concluded
   DEFAULT_COMPLETED_MAPS_PERCENT_FOR_REDUCE_SLOWSTART = 0.05f */
   
    if (job_mra.tasks_pending[MRA_REDUCE] <= 0 || (float)job_mra.tasks_pending[MRA_MAP]/config_mra.amount_of_tasks_mra[MRA_MAP] > 0.95)
			return;

    	enum { NORMAL, SPECULATIVE, NO_TASK };
    	task_type = NO_TASK;

    	for (t = 0; t < config_mra.amount_of_tasks_mra[MRA_REDUCE]; t++)
    		{
					if (job_mra.task_status[MRA_REDUCE][t] == T_STATUS_MRA_PENDING)
						{
	    				task_type = NORMAL;
	    				tid = t;
	    				break;
						}
					else if (job_mra.task_status[MRA_REDUCE][t] == T_STATUS_MRA_TIP_SLOW && job_mra.task_instances[MRA_REDUCE][t] < 2)
						{
	    				task_type = SPECULATIVE;
	    				tid = t;
						}
    		}
    	switch (task_type)
    		{
					case NORMAL:
	    		flags = "";
	    		stats_mra.reduce_mra_normal++;
	    		break;

					case SPECULATIVE:
	    		flags = "(speculative)";
	    		stats_mra.reduce_mra_spec++;
	    		break;

					default: return;
    		}

    XBT_INFO ("MRA_reduce %zu assigned to %s %s", tid, MSG_host_get_name (dest), flags);

    send_mra_task (MRA_REDUCE, tid, NONE, dest);
}

/**
 * @brief  Send a task to a worker.
 * @param  mra_phase     The current job phase.
 * @param  tid       The task ID.
 * @param  data_src  The ID of the DataNode that owns the task data.
 * @param  dest      The destination worker.
 */
static void send_mra_task (enum mra_phase_e mra_phase, size_t tid, size_t data_src, msg_host_t dest)
{
    char         mailbox[MAILBOX_ALIAS_SIZE];
    int          i;
    double       cpu_required = 0.0;
    msg_task_t   mra_task = NULL;
    mra_task_info_t  task_info;
    size_t       mra_wid;

    mra_wid = get_mra_worker_id (dest);

    cpu_required = user_mra.task_mra_cost_f (mra_phase, tid, mra_wid);

    task_info = xbt_new (struct mra_task_info_s, 1);
    mra_task = MSG_task_create (SMS_TASK_MRA, cpu_required, 0.0, (void*) task_info);

    task_info->mra_phase = mra_phase;
    task_info->mra_tid = tid;
    task_info->mra_src = data_src;
    task_info->mra_wid = mra_wid;
    task_info->mra_task = mra_task;
    task_info->shuffle_mra_end = 0.0;

    // for tracing purposes...
    MSG_task_set_category (mra_task, (mra_phase==MRA_MAP?"MRA_MAP":"MRA_REDUCE"));

    if (job_mra.task_status[mra_phase][tid] != T_STATUS_MRA_TIP_SLOW)
	job_mra.task_status[mra_phase][tid] = T_STATUS_MRA_TIP;

    job_mra.mra_heartbeats[mra_wid].slots_av[mra_phase]--;

    for (i = 0; i < MAX_SPECULATIVE_COPIES; i++)
    {
	if (job_mra.task_list[mra_phase][tid][i] == NULL)
	{
	    job_mra.task_list[mra_phase][tid][i] = mra_task;
	    break;
	}
    }

    fprintf (tasks_log, "%d_%zu_%d,%s,%zu,%.3f,START,\n", mra_phase, tid, i, (mra_phase==MRA_MAP?"MRA_MAP":"MRA_REDUCE"), mra_wid, MSG_get_clock ());

#ifdef VERBOSE
    XBT_INFO ("TX: %s > %s", SMS_TASK_MRA, MSG_host_get_name (dest));
#endif

    sprintf (mailbox, TASKTRACKER_MRA_MAILBOX, mra_wid);
    xbt_assert (MSG_task_send (mra_task, mailbox) == MSG_OK, "ERROR SENDING MESSAGE");

    job_mra.task_instances[mra_phase][tid]++;
}

/**
 * @brief  Kill all copies of a task.
 * @param  ti  The task information of any task instance.
 */
static void finish_all_mra_task_copies (mra_task_info_t ti)
{
    int     i;
    int     mra_phase = ti->mra_phase;
    size_t  tid = ti->mra_tid;

    for (i = 0; i < MAX_SPECULATIVE_COPIES; i++)
    {
			if (job_mra.task_list[mra_phase][tid][i] != NULL)
				{
	    //MSG_task_cancel (job.task_list[phase][tid][i]);
	    MSG_task_destroy (job_mra.task_list[mra_phase][tid][i]);
	    job_mra.task_list[mra_phase][tid][i] = NULL;
	    fprintf (tasks_log, "%d_%zu_%d,%s,%zu,%.3f,END,%.3f\n", ti->mra_phase, tid, i, (ti->mra_phase==MRA_MAP?"MRA_MAP":"MRA_REDUCE"), ti->mra_wid, MSG_get_clock (), ti->shuffle_mra_end);
				}
    }
}

