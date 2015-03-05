/*=============================================================================
#     FileName: gap_cv.cpp
#         Desc: 
#       Author: jlpeng
#        Email: jlpeng1201@gmail.com
#     HomePage: 
#      Created: 2014-09-22 14:48:20
#   LastChange: 2015-03-03 15:14:34
#      History:
=============================================================================*/
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <utility>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <pthread.h>
#include "tools.h"
#include "../svm/svm.h"
#include "extern_tools.h"
#include <ga/garandom.h>

using namespace std;


Sample train_set;
//map<string, map<int, float> > each_atom_y;
vector<float> population;
vector<float> cgp;
vector<vector<svm_problem*> > probs;
vector<int> num_xs;
vector<svm_parameter*> para;
vector<vector<int> > perm;
int num_types(0);
vector<int> num_each_sample;
int kernel_type(0);
vector<vector<vector<double> > > kernel_matrix;
int nfolds(5);
int repeat(1);
int nthread(1);
bool do_log(true);


void read_train_partition(string &infile);
void load_cgp(string &infile);
void print_null(const char *s) {}
//vector<float> construct_population();
void runCV(int repeat, int nthread);

int main(int argc, char *argv[])
{
    if(argc < 3) {
        cerr << endl << "  Usage: " << argv[0] << " [options] train_des train_partition" << endl
            << endl << "[options]" << endl
            << "  -n int      : <default 5>" << endl
            << "                do {n}-fold CV" << endl
            << "  --repeat int: <default 1>" << endl
            << "                do {n}-fold CV {repeat} times" << endl
            << "                each time, samples will be randomized" << endl
            << "                in this case, `--seed n` will be ignored" << endl
            << "  --nthread int: <default: 1>" << endl
            << "                number of threads" << endl
            << "  --cgp file  : where to load `c,g,p` for each atom type instead" << endl
            << "                each line should be `atom_type c g p`" << endl
            << "  --seed n    : specify the random seed" << endl
            << "  --kernel int: <default: 0>" << endl
            << "                0 - RBF kernel" << endl
            << "                1 - tanimoto kernel" << endl
            << "                2 - minMax kernel" << endl
            << "  --verbose   : if given, actual and predicted log(CL) will be shown" << endl
            << "  --no-log    : <optional>" << endl
            << "                if given, models are trained based on y instead of log10(y)" << endl
            << endl
            << "  train_des      : training set" << endl
            << "  train_partition: generated by ga_partition" << endl
            << "                   atom contribution will be read" << endl
            << "                   c,g,p will be used if `--cgp` not given" << endl
            << endl;
        exit(EXIT_FAILURE);
    }

    svm_set_print_string_function(print_null);

    unsigned int seed = 0;
    string cgp_file("");
    bool verbose(false);
    int i;
    cout << "CMD:";
    for(i=0; i<argc; ++i)
        cout << " " << argv[i];
    cout << endl;
    for(i=1; i<argc; ++i) {
        if(argv[i][0] != '-')
            break;
        if(strcmp(argv[i], "--cgp") == 0)
            cgp_file = argv[++i];
        else if(strcmp(argv[i], "--seed") == 0) {
            istringstream is(argv[++i]);
            is >> seed;
        }
        else if(strcmp(argv[i], "-n") == 0)
            nfolds = atoi(argv[++i]);
        else if(strcmp(argv[i], "--kernel") == 0)
            kernel_type = atoi(argv[++i]);
        else if(strcmp(argv[i], "--verbose") == 0)
            verbose = true;
        else if(strcmp(argv[i], "--repeat") == 0)
            repeat = atoi(argv[++i]);
        else if(strcmp(argv[i], "--nthread") == 0)
            nthread = atoi(argv[++i]);
        else if(strcmp(argv[i], "--no-log") == 0)
            do_log = false;
        else {
            cerr << "Error: invalid option " << argv[i] << endl;
            exit(EXIT_FAILURE);
        }
    }
    if(argc-i != 2) {
        cerr << "Error: invalid number of arguments" << endl;
        exit(EXIT_FAILURE);
    }
    string train_des(argv[i]);
    string train_partition(argv[i+1]);

    if(repeat > 1) {
        if(seed != 0) {
             cout << "Warning: `--repeat " << repeat << "` is given, so `--seed " << seed << "` is ignored" << endl;
             seed = 0;
        }
        if(verbose)
             cout << "Warning: `--repeat " << repeat << "` is given, so `--verbose` is ignored" << endl;
    }

    train_set.read_problem(train_des);

    randomize_samples(true);
    construct_svm_problems_parameters();

    read_train_partition(train_partition);
    cout << "len(population)=" << population.size() << endl;

    if(cgp_file != "")
        load_cgp(cgp_file);

    int start_cgp = train_set.count_total_num_atoms();
    for(i=0; i<num_types; ++i) {
        cout << "parameter for atom type " << i 
            << ": c=" << population[start_cgp+3*i] << ", g=" << population[start_cgp+3*i+1] 
            << ", p=" << population[start_cgp+3*i+2] << endl;
    }
    
    cout << "rmse r" << endl;
    runCV(repeat, nthread);

    free_svm_problems_parameters();
    
    return 0;
}

void read_train_partition(string &infile)
{
    ifstream inf(infile.c_str());
    if(!inf) {
        cerr << "Error: failed to open " << infile << endl;
        exit(EXIT_FAILURE);
    }
    population.clear();
    string line;
    getline(inf,line);
    istringstream is(line);
    float val;
    for(int i=0; i<train_set.count_total_num_atoms(); ++i) {
        is >> val;
        population.push_back(val);
    }
    while(is >> val)
        population.push_back(val);
    inf.close();
}

void load_cgp(string &infile)
{
    ifstream inf(infile.c_str());
    if(!inf) {
        cerr << "Error: failed to open " << infile << endl;
        exit(EXIT_FAILURE);
    }
    string line;
    cgp.clear();
    cgp.resize(num_types);
    while(getline(inf,line)) {
        if(line.size()==0 || line[0]=='#')
            continue;
        istringstream is(line);
        int _type;
        float c,g,p;
        is >> _type >> c >> g >> p;
        cgp[3*_type] = c;
        cgp[3*_type+1] = g;
        cgp[3*_type+2] = p;
    }
    inf.close();
}

class CandidateIndices
{
public:
    CandidateIndices(int n): _n(n), _i(-1) {}
    int next() {
        if(_i < _n-1)
            return ++_i;
        else
            return -1;
    }
private:
    int _n;
    int _i;
};
CandidateIndices *myIndex;
pthread_mutex_t mut;

static void do_each(int begin, int end, vector<double> &actualY, vector<PredictResult> &predictY, int ii)
{
    int i,j,k,idx_genome;
    for(i=0; i<num_types; ++i)
        probs[ii][i]->l = 0;

    if(para[ii]->kernel_type == PRECOMPUTED) {
        cerr << "Error: PRECOMPUTED kernel not supported!!!" << endl;
        exit(EXIT_FAILURE);
        /*
        vector<vector<vector<double> > > train_xs(num_types);
        vector<vector<double> > train_ys(num_types);
        // construct training set
        idx_genome = 0;
        for(i=0; i<train_set.num_samples(); ++i) {
            for(j=0; j<train_set[perm[i]].num_atoms; ++j) {
                int _type = train_set[perm[i]].atom_type[j];
                if(i<begin || i>=end) {
                    train_xs[_type].push_back(train_set[perm[i]].x[j]);
                    if(fraction)
                        train_ys[_type].push_back(log10(population[idx_genome]) + train_set[perm[i]].y);
                    else
                        train_ys[_type].push_back(population[idx_genome]);
                }
                ++idx_genome;
            }
        }
        for(i=0; i<num_types; ++i) {
            int num_train = static_cast<int>(train_xs[i].size());
            for(j=0; j<num_train; ++j) {
                probs[i]->x[probs[i]->l][0].index = 0;
                probs[i]->x[probs[i]->l][0].value = j+1;
                for(k=0; k<num_train; ++k) {
                    probs[i]->x[probs[i]->l][k+1].index = k+1;
                    probs[i]->x[probs[i]->l][k+1].value = calcKernel(train_xs[i][j], train_xs[i][k]);
                }
                probs[i]->x[probs[i]->l][k+1].index = -1;
                probs[i]->y[probs[i]->l] = train_ys[i][j];
                probs[i]->l++;
            }
        }
        // 2. train svm model
        vector<svm_model*> models(num_types, NULL);
        for(i=0; i<num_types; ++i) {
            para->C = population[idx_genome];
            para->gamma = population[idx_genome+1];
            para->p = population[idx_genome+2];
            idx_genome += 3;
            models[i] = svm_train(probs[i], para);
        }
        // 3. predict
        int max_xs = *max_element(num_each_sample.begin(), num_each_sample.end());
        struct svm_node *x = (struct svm_node*)malloc(sizeof(struct svm_node)*(max_xs+2));
        for(i=0; i<train_set.num_samples(); ++i) {
            if(i<begin || i>=end)
                continue;
            PredictResult val;
            val.y = 0.;
            //double val = 0.;
            for(j=0; j<train_set[perm[i]].num_atoms; ++j) {
                int _type = train_set[i].atom_type[j];
                x[0].index = 0;
                for(k=0; k<num_each_sample[_type]; ++k) {
                    x[k+1].index = k+1;
                    x[k+1].value = calcKernel(train_set[i].x[j], train_xs[_type][k]);
                }
                x[k+1].index = -1;
                double each_value = svm_predict(models[_type], x);
                val.each_y.push_back(each_value);
                val.y += pow(10, each_value);
                //val += pow(10, each_value);
            }
            if(val.y < 0)
                cout << "Warning(" << __FILE__ << ":" << __LINE__ << "): sum(10^eachy) < 0, may be out of range!" << endl;
            val.y = log10(val.y);
            actualY.push_back(train_set[perm[i]].y);
            predictY.push_back(val);
        }
        free(x);
        for(i=0; i<num_types; ++i)
            svm_free_and_destroy_model(&models[i]);
        */
    }
    else {
        // construct train and test set
        idx_genome = 0;
        for(i=0; i<train_set.num_samples(); ++i) {
            // test set
            if(i>=begin && i<end) {
                idx_genome += train_set[perm[ii][i]].num_atoms;
                continue;
            }
            // training set
            for(j=0; j<train_set[perm[ii][i]].num_atoms; ++j) {
                int _type = train_set[perm[ii][i]].atom_type[j];
                for(k=0; k<num_xs[_type]; ++k) {
                    probs[ii][_type]->x[probs[ii][_type]->l][k].index = k+1;
                    probs[ii][_type]->x[probs[ii][_type]->l][k].value = train_set[perm[ii][i]].x[j][k];
                }
                probs[ii][_type]->x[probs[ii][_type]->l][k].index = -1;
                if(do_log)
                    probs[ii][_type]->y[probs[ii][_type]->l] = log10(population[idx_genome]) + train_set[perm[ii][i]].y;
                else
                    probs[ii][_type]->y[probs[ii][_type]->l] = population[idx_genome]*pow(10,train_set[perm[ii][i]].y);
                probs[ii][_type]->l++;
                ++idx_genome;
            }
        }
        // train models
        vector<svm_model*> models(num_types, NULL);
        for(i=0; i<num_types; ++i) {
            para[ii]->C = population[idx_genome];
            para[ii]->gamma = population[idx_genome+1];
            para[ii]->p = population[idx_genome+2];
            idx_genome += 3;
            models[i] = svm_train(probs[ii][i], para[ii]);
        }

        // predict
        /*
        for(i=begin; i<end; ++i) {
            actualY.push_back(train_set[perm[i]].y);
            predictY.push_back(train_set[perm[i]].predict(models));
        }
        */
        int max_num_xs = *max_element(num_xs.begin(), num_xs.end());
        struct svm_node *x = (struct svm_node*)malloc(sizeof(struct svm_node)*(max_num_xs+1));
        for(i=begin; i<end; ++i) {
            PredictResult val;
            val.y = 0.;
            //double val = 0.;
            for(j=0; j<train_set[perm[ii][i]].num_atoms; ++j) {
                int _type = train_set[perm[ii][i]].atom_type[j];
                for(k=0; k<num_xs[_type]; ++k) {
                    x[k].index = k+1;
                    x[k].value = train_set[perm[ii][i]].x[j][k];
                }
                x[k].index = -1;
                double each_value = svm_predict(models[_type], x);
                if(do_log) {
                    val.each_y.push_back(each_value);
                    val.y += pow(10, each_value);
                }
                else {
                    if(each_value < 0) {
                        cout << "Warning(" << __FILE__ << ":" << __LINE__ << "): predicted atom contribution < 0" << endl;
                        val.each_y.push_back(each_value);
                    }
                    else
                        val.each_y.push_back(log10(each_value));
                    val.y += each_value;
                }
                if(!train_set[perm[ii][i]].som.empty())
                    val.som.push_back(train_set[perm[ii][i]].som[j]);
            }
            if(val.y < 0)
                cout << "Warning(" << __FILE__ << ":" << __LINE__ << "): predicted Cl is negative!!" << endl;
            val.y = log10(val.y);
            actualY.push_back(train_set[perm[ii][i]].y);
            predictY.push_back(val);
        }

        // free
        free(x);
        for(i=0; i<num_types; ++i)
            svm_free_and_destroy_model(&models[i]);
    }
    
}

static void *doCV(void *arg)
{
    int i,j;
    int n = train_set.num_samples();
    while(true) {
        pthread_mutex_lock(&mut);
        i = myIndex->next();
        pthread_mutex_unlock(&mut);
        if(i == -1)
            break;
        vector<double> actualY;
        vector<PredictResult> predictY;
        for(j=0; j<nfolds; ++j) {
            int begin = j*n/nfolds;
            int end   = (j+1)*n/nfolds;
            do_each(begin, end, actualY, predictY, i);
        }
        double r = calcR(actualY, predictY);
        double rmse = calcRMSE(actualY, predictY);
        pthread_mutex_lock(&mut);
        cout << rmse << " " << r << endl;
        pthread_mutex_unlock(&mut);
    }

	pthread_exit(0);
}

void runCV(int repeat, int nthread)
{
    myIndex = new CandidateIndices(repeat);

    pthread_t *thread = (pthread_t*)malloc(sizeof(pthread_t)*nthread);
    memset(thread, 0, sizeof(pthread_t)*nthread);
    for(int i=0; i<nthread; ++i) {
        int retval = pthread_create(&thread[i], NULL, doCV, NULL);
        if(retval)
            cerr << "Error: failed to create thread " << i+1 << endl;
    }
    for(int i=0; i<nthread; ++i) {
        int retval = pthread_join(thread[i], NULL);
        if(retval)
            cerr << "Error: failed to join thread" << i+1 << endl;
    }
    free(thread);
    delete myIndex;
}
