// Supervisor of the project
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <webots/robot.h>
#include <webots/emitter.h>
#include <webots/supervisor.h>

#define FLOCK_SIZE	5		// Number of robots in flock
#define TIME_STEP	64		// [ms] Length of time step
#define fit_cluster_ref 0.03
#define fit_orient_ref 1.0


#define MAX_SPEED         0.1287     // Maximum speed [m/s]

#define RULE1_THRESHOLD 0.2

WbNodeRef robs[FLOCK_SIZE];		// Robots nodes
WbFieldRef robs_trans[FLOCK_SIZE];	// Robots translation fields
WbFieldRef robs_rotation[FLOCK_SIZE];	// Robots rotation fields
WbDeviceTag emitter;			// Single emitter

float loc[FLOCK_SIZE][3];		// Location of everybody in the flock
float speed[FLOCK_SIZE];                    // Speed of everybody in the flock

int offset;			// Offset of robots number
float migrx, migrz;			// Migration vector
float orient_migr; 			// Migration orientation
int t;

/*
 * Initialize flock position and devices
 */
void reset(void) {
    wb_robot_init();
    
    emitter = wb_robot_get_device("emitter");
    if (emitter == 0) printf("missing emitter\n");
    	
    char rob[7] = "epuck0";
    
    for (int i=0; i<FLOCK_SIZE; i++) {
        sprintf(rob,"epuck%d",i+offset);
        robs[i] = wb_supervisor_node_get_from_def(rob);
        robs_trans[i] = wb_supervisor_node_get_field(robs[i],"translation");
        robs_rotation[i] = wb_supervisor_node_get_field(robs[i],"rotation");
    }
}

/*
 * Send initialize position
 */
void send_init_poses(void) {
    char buffer[255];	// Buffer for sending data
           
    for (int i=0; i<FLOCK_SIZE; i++) {
        // Get data
        loc[i][0] = wb_supervisor_field_get_sf_vec3f(robs_trans[i])[0]; // X
        loc[i][1] = wb_supervisor_field_get_sf_vec3f(robs_trans[i])[2]; // Z
        loc[i][2] = wb_supervisor_field_get_sf_rotation(robs_rotation[i])[3]; // THETA
      
        // Send it out
        sprintf(buffer,"%1d#%f#%f#%f##%f#%f",i+offset,loc[i][0],loc[i][1],loc[i][2], migrx, migrz);
        wb_emitter_send(emitter,buffer,strlen(buffer));
      
        // Run one step
        wb_robot_step(TIME_STEP);
    }
}

/*
 * Compute performance metric.
 */
void compute_fitness(float *p_t, float *p_mean) {
    float mean_x = 0.0, mean_z = 0.0, old_mean_x = 0.0, old_mean_z = 0.0;
    float dist = 0.0;
    float fit_c, fit_o, fit_s;
    static float im_o, re_o;
    
    // Orientation between robots
    for(int i=0; i<FLOCK_SIZE; i++){
        re_o += cosf(loc[i][2]);
        im_o += sinf(loc[i][2]);
    }
    
    fit_o = sqrtf(re_o*re_o + im_o*im_o);
    
    // Distance between robots
    for(int i=0; i<FLOCK_SIZE; i++){
        mean_x += loc[i][0];
        mean_z += loc[i][1];
    }
    
    mean_x /= FLOCK_SIZE;
    mean_z /= FLOCK_SIZE;
    
    for(int i=0; i<FLOCK_SIZE; i++){
        dist += sqrtf((loc[i][0] - mean_x)*(loc[i][0] - mean_x) + (loc[i][1] - mean_z)*(loc[i][1] - mean_z));
    }
    
    fit_c = 1/(dist/FLOCK_SIZE + 1);
    
    // Velocity of the team towards the goal direction
    float max_a = cosf(orient_migr)*sqrtf((mean_x - old_mean_x)*(mean_x - old_mean_x) +
                                          (mean_z - old_mean_z)*(mean_z - old_mean_z))/MAX_SPEED;
    fit_s = (max_a < 0) ? 0 : max_a;

    old_mean_x = mean_x;
    old_mean_z = mean_z;
    
    // Return performance metrix
    *p_t = fit_c*fit_o*fit_s;
    *p_mean = (*p_mean*(t-TIME_STEP) + *p_t)/t;

    // Compute performance indices
    // Based on distance of the robots compared to the threshold and the deviation from the perfect angle towards
    // the migration goal
    /*float angle_diff;
    int i; int j;
    
    for (i=0;i<FLOCK_SIZE;i++) {
        for (j=i+1;j<FLOCK_SIZE;j++) {	
            // Distance measure for each pair ob robots
            *fit_c += fabs(sqrtf(powf(loc[i][0]-loc[j][0],2)+powf(loc[i][1]-loc[j][1],2)));
        }
    
        // Angle measure for each robot
        angle_diff = fabsf(loc[i][2]-orient_migr);
        *fit_o += angle_diff > M_PI ? 2*M_PI-angle_diff : angle_diff;
    }
    
    *fit_c /= FLOCK_SIZE*(FLOCK_SIZE+1)/2;
    *fit_o /= FLOCK_SIZE;*/
}

/*
 * Main function.
 */
int main(int argc, char *args[]) {
    char buffer[255];	// Buffer for sending data
    
    if (argc == 4) { // Get parameters
        offset = atoi(args[1]);
        migrx = atof(args[2]);
        migrz = atof(args[3]);
        
        //migration goal point comes from the controller arguments. It is defined in the world-file, under "controllerArgs" of the supervisor.
        printf("Offset : %d\t", offset);
        printf("Migratory instinct : (%f, %f)\n", migrx, migrz);
    } else {
        printf("Missing argument\n");
        return 1;
    }
    
    orient_migr = -atan2f(migrx,migrz);
    
    if (orient_migr<0) {
        orient_migr+=2*M_PI; // Keep value within 0, 2pi
    }
    
    reset();
    send_init_poses();
	
    // Compute reference fitness values
    float p_t = 0.0, p_mean = 0.0;
    		
    for(;;) {
        wb_robot_step(TIME_STEP);
    		
        if (t % 10 == 0) {
            for (int i=0; i<FLOCK_SIZE; i++) {
                // Get data
                loc[i][0] = wb_supervisor_field_get_sf_vec3f(robs_trans[i])[0]; // X
                loc[i][1] = wb_supervisor_field_get_sf_vec3f(robs_trans[i])[2]; // Z
                loc[i][2] = wb_supervisor_field_get_sf_rotation(robs_rotation[i])[3]; // THETA
                				
                // Sending positions to the robots, comment the following two lines if you don't want the supervisor sending it                   		
                sprintf(buffer,"%1d#%f#%f#%f##%f#%f",i+offset,loc[i][0],loc[i][1],loc[i][2], migrx, migrz);
                wb_emitter_send(emitter,buffer,strlen(buffer));				
            }
            
            //Compute and normalize fitness values
            compute_fitness(&p_t, &p_mean);			
        			
        }
    		
        t += TIME_STEP;
    }

}