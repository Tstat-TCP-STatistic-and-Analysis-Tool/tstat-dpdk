#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <rrd.h>
#include <math.h>
#include <float.h>
#include <sys/stat.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

//Program constants
#define DIR_LENGTH 1000
#define MAX_DIRS 16
#define MAX_NAME 100
#define MAX_IDX 100
#define MAX_PRC 100
#define MAX_MEASUREMENT 1000
#define N_RRA 4
#define HISTO 1
#define MIN 2
#define MAX 3

//Debeug level
#define DEBUG 2

//Struct wrapping files data
typedef struct s_measurement{
    char name [MAX_NAME];
    int min;
    int max;
    int avg;
    int stdev;
    int idx[MAX_IDX];
    int idxoth;
    int num_idx;
    int prc[MAX_PRC];
    int num_prc;
}measurement;

//Static variables
char outdir [DIR_LENGTH];
char indir [MAX_DIRS][DIR_LENGTH];
int n_dir = 0;
measurement meas [MAX_MEASUREMENT];
int n_meas=0;

// Prototypes
void parse_args (int argc, char *argv[]);
void parse_file (char * name );
int  create_new_measure(char *);
void create_metadata(char * directory);
void update_rrd(void);
void update_rrd_file(char * name, int type);
void sum_rras(xmlNodePtr * nodes, int n_nodes);
void min_rras(xmlNodePtr * nodes, int n_nodes);
void max_rras(xmlNodePtr * nodes, int n_nodes);
void avg_rras(xmlNodePtr * nodes, int n_nodes);


int main(int argc, char *argv[])
{
    
    parse_args(argc, argv);

    //Build files index on first input directory
    create_metadata(indir[0]);

    update_rrd();

    return(0);
}

void parse_args(int argc, char *argv[]){

    int i;

    //Wrong arguments
    if (argc < 4){
        printf("Wrong arguments: usage rrd_merger outdir indir1 indir2 ...\n");
        exit(1);
    }

    //Copy arguments
    strcpy(outdir, argv[1]);
    n_dir=argc-2;
    for (i=2; i<argc; i++)
        strcpy(indir[i-2], argv[i]);

    //Output directories
    if (DEBUG > 0){
        printf("Output directory: %s\n", outdir);
        printf("Input directories: ");
        for (i=0;i<n_dir;i++)
            printf("%s ", indir[i]);
        printf("\n");
    }

    return;

}

void create_metadata(char * directory){
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir (directory)) != NULL) {

        //For each file in the directory
        while ((ent = readdir (dir)) != NULL) {

            // Skip ".." and "."
            if ( strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0 )
                continue;

            if (DEBUG > 1)
                printf ("Detected input file: %s\n", ent->d_name);
            parse_file(ent->d_name);
        }

        closedir (dir);

    } else {
        /* could not open directory */
        printf("Cannot open directory %s\n", directory);
        exit (1);
    }

        if (DEBUG > 0)
        printf("Metadata created\n");
}

void parse_file(char * file_name ){

    char * name;
    char * type;
    int i;
    int num;
    int meas_index = -1;

    // Parse name file
    strtok(file_name, ".");
    name=strtok(file_name, ".");
    strtok(name+strlen(name)+1, ".");
    type=name+strlen(name)+1;

    // Check if the measure with this name exists
    for(i=0;i<n_meas;i++){
        if(strcmp(name,meas[i].name)==0){
            meas_index=i;
            i=n_meas;
            }
    }
    
    // Create entry if it doesn't exists
    if (meas_index==-1)
        meas_index = create_new_measure(name);

    // Detect measurement type associated to this file
    if (strcmp(type, "min")==0){
        meas[meas_index].min=1;
    }
    else if (strcmp(type, "max")==0){
        meas[meas_index].max=1;
    }
    else if (strcmp(type, "avg")==0){
        meas[meas_index].avg=1;
    }
    else if (strcmp(type, "stdev")==0){
        meas[meas_index].stdev=1;
    }
    else if (strcmp(type, "idxoth")==0){
        meas[meas_index].idxoth=1;
    }
    if(type[0]=='i' && type[1]=='d' && type[2]=='x' && strlen(type)>3 && type[3]!='o'){
        num=atoi(&type[3]);
        meas[meas_index].idx[meas[meas_index].num_idx]=num;
        meas[meas_index].num_idx++;
    }
    if(type[0]=='p' && type[1]=='r' && type[2]=='c' && strlen(type)>3 ){
        num=atoi(&type[3]);
        meas[meas_index].prc[meas[meas_index].num_prc]=num;
        meas[meas_index].num_prc++;
    }

}

// Create new entry in measurements vector
int create_new_measure(char * name){

    memset(&meas[n_meas],0,sizeof(measurement));
    strcpy(meas[n_meas].name, name);
    n_meas++;
    return n_meas-1;
}


void update_rrd(void){

    int i,j;
    char name [MAX_NAME];

    //Create directory if doesn't exists
    mkdir(outdir, 0777);

    //Executing correct merge operation according to measurement type
    for (i=0; i<n_meas; i++){
        if (meas[i].min){
            sprintf(name, "%s.min", meas[i].name);
            if (DEBUG > 1)
                printf("Updating file: %s.rrd\n", name);
            update_rrd_file(name, MIN);
        }
        if (meas[i].max){
            sprintf(name, "%s.max", meas[i].name);
            if (DEBUG > 1)
                printf("Updating file: %s.rrd\n", name);
            update_rrd_file(name, MAX);
        }
        if (meas[i].avg){
        }
        if (meas[i].stdev){
        }
        if (meas[i].idxoth){
            sprintf(name, "%s.idxoth", meas[i].name);
            if (DEBUG > 1)
                printf("Updating file: %s.rrd\n", name);
            update_rrd_file(name, HISTO);
        }
        for (j=0; j<meas[i].num_idx;j++){
            sprintf(name, "%s.idx%d", meas[i].name,meas[i].idx[j]);
            if (DEBUG > 1)
                printf("Updating file: %s.rrd\n", name);
            update_rrd_file(name, HISTO);

        }
        for (j=0; j<meas[i].num_prc;j++){


        }
    }

}


void update_rrd_file(char * name, int type){

    char command [MAX_NAME*2], file_out [MAX_NAME];
    int i, j, fd;
    FILE * fp;
    xmlDocPtr trees [MAX_DIRS];
    xmlNodePtr rras [MAX_DIRS];

    //Convert in xml each input file
    //The merge is performed using the first input file as a template.
    for (i=0;i<n_dir;i++){

        //Open xml data
        sprintf(command, "rrdtool dump %s/%s.rrd", indir[i],name);
        fp = popen(command, "r");
        fd = fileno(fp);

        //Parse it
        trees[i]= xmlReadFd	( fd,  "", NULL, 0);
        if (trees[i] == NULL) {
            printf("error: could not parse file %s/%s.rrd\n", indir[i],name);
            return;
        }

        pclose (fp);

    }

    // Get first RRA (measurement block) of each file
    for (i=0;i<n_dir;i++)
        for(rras[i]=xmlDocGetRootElement(trees[i])->children;strcmp((char*)rras[i]->name, "rra")!=0; rras[i]=rras[i]->next){}

    // Merge every RRA (measurement block) according to measurement type. The merge is performed using the first input file as a template.
    for (i=0;i<N_RRA;i++){

        if (type == HISTO)
            sum_rras(rras, n_dir);
        else if (type == MIN)
            min_rras(rras, n_dir);
        else if (type == MAX)
            max_rras(rras, n_dir);

        // Pass to next RRAs (measurement blocks) of each file
        for(j=0; j< n_dir; j++){
            rras[j]=rras[j]->next;
            while ( rras[j] != NULL && strcmp((char*)rras[j]->name, "rra")!=0 ){ rras[j]=rras[j]->next;}
        }

    }

    // Create XML output
    sprintf(file_out, "%s/%s.xml",  outdir, name);
    xmlSaveFileEnc(file_out, trees[0], "UTF-8");
    for (i=0; i< n_dir; i++)
        xmlFreeDoc(trees[i]);

    // Convert it to RRD format and remove XML file
    sprintf(command, "rrdtool restore %s/%s.xml %s/%s.rrd -f",  outdir, name,  outdir, name);
    system (command);
    remove(file_out);

}


void sum_rras(xmlNodePtr * nodes, int n_nodes){

    xmlNodePtr rows [MAX_DIRS], tmp, dst;
    double sum, val;
    int i;
    char * content;
    char buffer [MAX_NAME];

    // Get first row of each RRA
    for (i=0; i<n_nodes;i++){
        for(tmp = nodes[i]->children; strcmp((char*)tmp->name, "database")!=0; tmp=tmp->next){}
        rows[i]=tmp->children;

        while ( strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;

    }

    // For each row
    while( rows[0] != NULL ){

        sum = 0;

        // Calculate the sum
        for (i = 0; i < n_nodes; i++){
            content = (char*)xmlNodeGetContent (rows[i]->children);
            sscanf( content, "%lf", &val) ;
            xmlFree(content);
            sum += val;

            if (i==0){
                dst = rows[0]->children;
            }

            rows[i]=rows[i]->next;
            while ( rows[i] != NULL && strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;


        }

        // Write it on first RRA
        sprintf(buffer, "%e", sum);
        xmlNodeSetContent(dst, (xmlChar*)buffer);


    }


}

void min_rras(xmlNodePtr * nodes, int n_nodes){

    xmlNodePtr rows [MAX_DIRS], tmp, dst;
    double min, val;
    int i;
    char * content;
    char buffer [MAX_NAME];

    // Get first row of each RRA
    for (i=0; i<n_nodes;i++){
        for(tmp = nodes[i]->children; strcmp((char*)tmp->name, "database")!=0; tmp=tmp->next){}
        rows[i]=tmp->children;

        while ( strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;

    }

    // For each row
    while( rows[0] != NULL ){

        min = DBL_MAX;

        // Calculate the min
        for (i = 0; i < n_nodes; i++){
            content = (char*)xmlNodeGetContent (rows[i]->children);
            sscanf( content, "%lf", &val) ;
            xmlFree(content);
            if (val<min) min = val;

            if (i==0){
                dst = rows[0]->children;
            }

            rows[i]=rows[i]->next;
            while ( rows[i] != NULL && strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;


        }

        // Write it on first RRA
        sprintf(buffer, "%e", min);
        xmlNodeSetContent(dst, (xmlChar*)buffer);


    }


}

void max_rras(xmlNodePtr * nodes, int n_nodes){

    xmlNodePtr rows [MAX_DIRS], tmp, dst;
    double max, val;
    int i;
    char * content;
    char buffer [MAX_NAME];

    // Get first row of each RRA
    for (i=0; i<n_nodes;i++){
        for(tmp = nodes[i]->children; strcmp((char*)tmp->name, "database")!=0; tmp=tmp->next){}
        rows[i]=tmp->children;

        while ( strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;

    }

    // For each row
    while( rows[0] != NULL ){

        max = DBL_MIN;

        // Calculate the max
        for (i = 0; i < n_nodes; i++){
            content = (char*)xmlNodeGetContent (rows[i]->children);
            sscanf( content, "%lf", &val) ;
            xmlFree(content);
            if (val>max) max = val;

            if (i==0){
                dst = rows[0]->children;
            }

            rows[i]=rows[i]->next;
            while ( rows[i] != NULL && strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;


        }

        // Write it on first RRA
        sprintf(buffer, "%e", max);
        xmlNodeSetContent(dst, (xmlChar*)buffer);


    }


}

void avg_rras(xmlNodePtr * nodes, int n_nodes){

    xmlNodePtr rows [MAX_DIRS], tmp, dst;
    double sum, val;
    int i;
    char * content;
    char buffer [MAX_NAME];

    // Get first row of each RRA
    for (i=0; i<n_nodes;i++){
        for(tmp = nodes[i]->children; strcmp((char*)tmp->name, "database")!=0; tmp=tmp->next){}
        rows[i]=tmp->children;

        while ( strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;

    }

    // For each row
    while( rows[0] != NULL ){

        sum = 0;

        // Calculate the sum
        for (i = 0; i < n_nodes; i++){
            content = (char*)xmlNodeGetContent (rows[i]->children);
            sscanf( content, "%lf", &val) ;
            xmlFree(content);
            sum += val;

            if (i==0){
                dst = rows[0]->children;
            }

            rows[i]=rows[i]->next;
            while ( rows[i] != NULL && strcmp((char*)rows[i]->name, "row")!=0 )rows[i]=rows[i]->next;


        }

        // Write it on first RRA
        sprintf(buffer, "%e", sum/ n_nodes);
        xmlNodeSetContent(dst, (xmlChar*)buffer);


    }


}

