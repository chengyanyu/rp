/*=============================================================================
#     FileName: gap_train.cpp
#         Desc: 
#       Author: jlpeng
#        Email: jlpeng1201@gmail.com
#     HomePage: 
#      Created: 2014-09-20 11:19:23
#   LastChange: 2014-10-10 13:48:27
#      History:
=============================================================================*/
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <utility>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include "tools.h"
#include "../svm/svm.h"
#ifdef NTHREAD
#include <pthread.h>
#endif

using namespace std;

string outname("");
vector<svm_problem*> probs;
//vector<vector<double> > cgp;
bool do_search(true);
int kernel_type(0);
vector<float> population;
vector<float> cgp;
bool do_log(true);

//void read_train_partition(const char *infile, map<string, map<int, double> > &partition);
void read_train_partition(const char *infile, int n);
//void create_svm_problems(Sample &train_set, map<string, map<int, double> > &partition);
void create_svm_problems(Sample &train_set, vector<float> &population);
void train_save_svm_models();
void print_null(const char *s) {}

int main(int argc, char *argv[])
{
    if(argc<4 || argc>8) {
        cerr << endl << "  Usage: " << argv[0] << " [options] train_des train_partition model_name" << endl
            << endl << "[options]" << endl
            << "  --nosearch: if given, no grid search will be done" << endl
            << "              instead, the c,g,p in `train_partition` will be used" << endl
            << "  --kernel type: <default 0>" << endl
            << "    0 - RBF" << endl
            << "    1 - precomputed tanimoto" << endl
            << "    2 - precomputed minMax" << endl
            << "  --no-log     : <optional>" << endl
            << "    if given, models will be trained on y instead of log10(y)" << endl
            << "  train_des: descriptor(fingerprint) set" << endl
            << "  train_partition: generated by `ga_partition`" << endl
            << "  model_name: model files will be saved as {model_name}_x" << endl
            << "              where, 'x' refers to the atom type, e.g. 0,1,2,3,4,5,6" << endl
#ifdef NTHREAD
            << endl << "  Attention:" << endl
            << "  1. this is multithread version" << endl
            << "     parallelly train svm model (grid search) for each atom type" << endl
#endif
            << endl;
        exit(EXIT_FAILURE);
    }

    svm_set_print_string_function(print_null);

    int i;
    for(i=1; i<argc; ++i) {
        if(argv[i][0] != '-')
            break;
        if(strcmp(argv[i], "--nosearch") == 0)
            do_search = false;
        else if(strcmp(argv[i], "--kernel") == 0)
            kernel_type = atoi(argv[++i]);
        else if(strcmp(argv[i], "--no-log") == 0)
            do_log = false;
        else {
            cerr << "Error: invalid option " << argv[i] << endl;
            exit(EXIT_FAILURE);
        }
    }
    if(argc-i != 3) {
        cerr << "Error: invalid number of arguments" << endl;
        exit(EXIT_FAILURE);
    }
    if(kernel_type<0 || kernel_type>3) {
        cerr << "Error: kernel type should be one of 0,1,2, but " << kernel_type << " is given" << endl;
        exit(EXIT_FAILURE);
    }

    Sample train_set;
    train_set.read_problem(argv[i]);

    // read train_partition
    //map<string, map<int, double> > partition;
    //read_train_partition(argv[i+1], partition);
    read_train_partition(argv[i+1], train_set.count_total_num_atoms());

    // create svm_problems
    //create_svm_problems(train_set, partition);
    create_svm_problems(train_set, population);
#ifdef DEBUG
    for(vector<svm_problem*>::size_type j=0; j<probs.size(); ++j) {
        ostringstream os;
        os << argv[i+2] << "_" << j << ".svm";
        ofstream outf(os.str().c_str());
        for(int k=0; k<probs[j]->l; ++k) {
            outf << probs[j]->y[k];
            int z=0;
            while(probs[j]->x[k][z].index != -1) {
                outf << " " << z+1 << ":" << probs[j]->x[k][z].value;
                ++z;
            }
            outf << endl;
        }
        outf.close();
    }
#endif

    // train svm model for each atom type
    outname = argv[i+2];
    train_save_svm_models();

    // free memory
    for(vector<svm_problem*>::size_type j=0; j<probs.size(); ++j) {
        free(probs[j]->y);
        for(int k=0; k<probs[j]->l; ++k)
            free(probs[j]->x[k]);
        free(probs[j]->x);
        free(probs[j]);
    }
    
    return 0;
}

/*
// called by `read_train_partition`
// to extract `atom_id:atom_value`
static void extract_each_value(string &line, string::size_type *i, map<int, double> &each_mol)
{
    string::size_type j = line.find(':', *i);
    int atom_id = atoi(line.substr(*i, j-*i).c_str());
    string::size_type k = line.find(' ', j+1);
    double cl;
    if(k == string::npos) {
        cl = atof(line.substr(j+1).c_str());
        *i = line.size();
    }
    else {
        cl = atof(line.substr(j+1, k-j-1).c_str());
        *i = k+1;
    }
    each_mol.insert(make_pair(atom_id, cl));
}
// key=mol_name, value={key=atom_id, value=log10(CL)}
void read_train_partition(const char *infile, map<string, map<int, double> > &result)
{
    result.clear();
    ifstream inf(infile);
    if(!inf) {
        cerr << "Error: failed to read " << infile << endl;
        exit(EXIT_FAILURE);
    }
    string line;
    while(getline(inf, line)) {
        if(line[0] == 'c')
            break;
        string::size_type i = line.find(' ');
        string name = line.substr(0,i);
        map<int, double> each_mol;
        i++;
        while(i < line.size())
            extract_each_value(line, &i, each_mol);
        result.insert(make_pair(name, each_mol));
    }
    cgp.clear();
    while(!inf.eof()) {
        string t1,t2,t3;
        istringstream is(line);
        is >> t1 >> t2 >> t3;
        vector<double> temp(3,0.);
        temp[0] = atof(t1.substr(2).c_str());
        temp[1] = atof(t2.substr(2).c_str());
        temp[2] = atof(t3.substr(2).c_str());
        cgp.push_back(temp);
        getline(inf,line);
    }
    inf.close();

}
*/
void read_train_partition(const char *infile, int n)
{
    ifstream inf(infile);
    if(!inf) {
        cerr << "Error: failed to open " << infile << endl;
        exit(EXIT_FAILURE);
    }
    string line;
    getline(inf, line);
    istringstream is(line);
    float val;
    population.clear();
    cgp.clear();
    for(int i=0; i<n; ++i) {
        is >> val;
        population.push_back(val);
    }
    while(is >> val)
        cgp.push_back(val);
    inf.close();

}

double calcKernel(vector<double> &x1, vector<double> &x2)
{
    if(kernel_type == 1)
        return tanimotoKernel(x1,x2);
    else if(kernel_type == 2)
        return minMaxKernel(x1,x2);
    else {
        cerr << "Error: calcKernel only supports kernel_type 1 or 2, but currently is " << kernel_type << endl;
        exit(EXIT_FAILURE);
    }
}
//void create_svm_problems(Sample &train_set, map<string, map<int, double> > &partition)
void create_svm_problems(Sample &train_set, vector<float> &population)
{
    int capacity=50;
    int max_type=-1;
    int i,j,k;
    // 1. count number of Xs for each atom type
    vector<int> num_xs(50,0);
    for(i=0; i<train_set.num_samples(); ++i) {
        for(j=0; j<train_set[i].num_atoms; ++j) {
            int _type = train_set[i].atom_type[j];
            if(_type >= capacity) {
                capacity *= 2;
                num_xs.resize(capacity,0);
            }
            num_xs[_type] = train_set[i].x[j].size();
            if(_type > max_type)
                max_type = _type;
        }
    }
    // 2. count number of Ys for each atom type
    vector<int> num_each_sample(max_type+1, 0);
    for(i=0; i<train_set.num_samples(); ++i)
        for(j=0; j<train_set[i].num_atoms; ++j)
            num_each_sample[train_set[i].atom_type[j]] += 1;
    // 3. create svm_problem for each atom type  -- allocate memory
    probs.resize(max_type+1, NULL);
    for(i=0; i<=max_type; ++i) {
        cout << "num_xs[" << i << "]=" << num_xs[i] << endl;
        probs[i] = (svm_problem*)malloc(sizeof(svm_problem));
        probs[i]->l = 0;
        probs[i]->y = (double*)malloc(sizeof(double)*(num_each_sample[i]));
        probs[i]->x = (svm_node**)malloc(sizeof(svm_node*)*(num_each_sample[i]));
        for(j=0; j<num_each_sample[i]; ++j) {
            if(kernel_type == 0)
                probs[i]->x[j] = (svm_node*)malloc(sizeof(svm_node)*(num_xs[i]+1));
            else
                probs[i]->x[j] = (svm_node*)malloc(sizeof(svm_node)*(num_each_sample[i]+2));
        }
    }
    // -- insert values
    if(kernel_type == 0) {
        int z = 0;
        for(i=0; i<train_set.num_samples(); ++i) {
            for(j=0; j<train_set[i].num_atoms; ++j) {
                int _type = train_set[i].atom_type[j];
                //probs[_type]->y[probs[_type]->l] = partition[train_set[i].name][train_set[i].atom_id[j]];
                if(do_log)
                    probs[_type]->y[probs[_type]->l] = log10(population[z++])+train_set[i].y;
                else
                    probs[_type]->y[probs[_type]->l] = population[z++]*pow(10,train_set[i].y);
                for(k=0; k<num_xs[_type]; ++k) {
                    probs[_type]->x[probs[_type]->l][k].index = k+1;
                    probs[_type]->x[probs[_type]->l][k].value = train_set[i].x[j][k];
                }
                probs[_type]->x[probs[_type]->l][num_xs[_type]].index = -1;
                probs[_type]->l++;
            }
        }
    }
    else {
        for(i=0; i<=max_type; ++i) {
            // extract fingerprints
            vector<vector<double> > tempX;
            vector<double> each_y;
            int z=0;
            for(j=0; j<train_set.num_samples(); ++j) {
                for(k=0; k<train_set[j].num_atoms; ++k) {
                    if(train_set[j].atom_type[k] == i) {
                        tempX.push_back(train_set[j].x[k]);
                        //each_y.push_back(partition[train_set[j].name][train_set[j].atom_id[k]]);
                        if(do_log)
                            each_y.push_back(log10(population[z])+train_set[j].y);
                        else
                            each_y.push_back(population[z]*pow(10,train_set[j].y));
                    }
                    ++z;
                }
            }
            // pre-calculate kernel matrix and fill svm-problems
            int num_train = static_cast<int>(tempX.size());
            for(j=0; j<num_train; ++j) {
                probs[i]->y[probs[i]->l] = each_y[j];
                probs[i]->x[probs[i]->l][0].index = 0;
                probs[i]->x[probs[i]->l][0].value = j+1;
                for(k=0; k<num_train; ++k) {
                    probs[i]->x[probs[i]->l][k+1].index = k+1;
                    probs[i]->x[probs[i]->l][k+1].value = calcKernel(tempX[j],tempX[k]);
                }
                probs[i]->x[probs[i]->l][k+1].index = -1;
                probs[i]->l++;
            }
        }
    }
    // -- check validation
    for(i=0; i<max_type+1; ++i) {
        if(probs[i]->l != num_each_sample[i]) {
            cerr << "Error: probs[" << i << "]->l =" << probs[i]->l << "  !=  num_each_sample[" << i << "] ="
                << num_each_sample[i] << endl;
            exit(EXIT_FAILURE);
        }
    }

}

static svm_parameter* create_svm_parameter()
{
    svm_parameter *para = (struct svm_parameter*)malloc(sizeof(struct svm_parameter));
    para->svm_type = EPSILON_SVR;
    if(kernel_type == 0)
        para->kernel_type = RBF;
    else
        para->kernel_type = PRECOMPUTED;
    para->degree = 3;
    para->gamma = 0.;
    para->coef0 = 0;
    para->cache_size = 100;
    para->eps = 0.001;
    para->C = 1.0;
    para->nr_weight = 0;
    para->weight_label = NULL;
    para->weight = NULL;
    para->nu = 0.5;
    para->p = 0.1;
    para->shrinking = 1;
    para->probability = 0;
    return para;
}

double eval(const double *actualY, const double *predictY, int n)
{
    double val = 0.;
    for(int i=0; i<n; ++i)
        val += pow(actualY[i]-predictY[i], 2);
    return sqrt(val / n);
}
double grid_search(svm_problem *prob, svm_parameter *para)
{
    double best_c(1.0), best_g(1.0), best_p(1.0);
    double best_val(1e38);
    double *target = (double*)malloc(sizeof(double)*(prob->l));

    for(int i=1; i<=5; ++i) {
        para->p = 0.05*i;
        for(int j=-8; j<=8; ++j) {
            para->gamma = pow(2., j);
            for(int k=-8; k<=8; ++k) {
                para->C = pow(2., k);
                svm_cross_validation(prob, para, 5, target);
                double cur_val = eval(prob->y, target, prob->l);
                if(cur_val < best_val) {
                    best_c = para->C;
                    best_g = para->gamma;
                    best_p = para->p;
                    best_val = cur_val;
                }
            }
        }
    }

    para->C = best_c;
    para->gamma = best_g;
    para->p = best_p;

    free(target);
    return best_val;
}

#ifdef NTHREAD
pthread_mutex_t mut;
void *train_each(void *arg)
{
    int i = *(int*)arg;
    svm_parameter *para = create_svm_parameter();
    pthread_mutex_lock(&mut);
    cout << "do grid search for atom type " << i << endl
        << "number of samples: " << probs[i]->l << endl;
    pthread_mutex_unlock(&mut);
    double rmse = grid_search(probs[i], para);
    pthread_mutex_lock(&mut);
    cout << "best svm parameter for atom type " << i << ":  "
        << "c=" << para->C << ", g=" << para->gamma << ", p=" << para->p
        << ",  rmse=" << rmse << endl;
    pthread_mutex_unlock(&mut);
    svm_model *model = svm_train(probs[i], para);
    ostringstream os;
    os << outname << "_" << i;
    svm_save_model(os.str().c_str(), model);
    svm_destroy_param(para);
    svm_free_and_destroy_model(&model);

    pthread_exit(0);
}
void train_save_svm_models()
{
    int n = static_cast<int>(probs.size());
    int i;
    vector<int> prob_index(n);
    for(i=0; i<n; ++i)
        prob_index[i] = i;

    pthread_t *thread = (pthread_t*)malloc(sizeof(pthread_t)*n);
    memset(thread, 0, sizeof(pthread_t)*n);
    for(i=0; i<n; ++i) {
        int retval = pthread_create(&thread[i], NULL, train_each, &prob_index[i]);
        if(retval)
            cerr << "Error: failed to create thread " << i+1 << endl;
    }
    for(i=0; i<n; ++i) {
        int retval = pthread_join(thread[i], NULL);
        if(retval)
            cerr << "Error: failed to join thread" << i+1 << endl;
    }
    free(thread);
}

#else
void train_save_svm_models()
{
    svm_parameter *para = create_svm_parameter();
    for(vector<svm_problem*>::size_type i=0; i<probs.size(); ++i) {
        if(do_search) {
            cout << "do grid search for atom type " << i << endl
                << "number of samples: " << probs[i]->l << endl;
            double rmse = grid_search(probs[i], para);
            cout << "best svm parameter for atom type " << i << ":  "
                << "c=" << para->C << ", g=" << para->gamma << ", p=" << para->p
                << ",  rmse=" << rmse << endl;
        }
        else {
            //para->C = cgp[i][0];
            //para->gamma = cgp[i][1];
            //para->p = cgp[i][2];
            para->C = cgp[3*i];
            para->gamma = cgp[3*i+1];
            para->p = cgp[3*i+2];
            cout << "cgp for atom type " << i << endl
                << "c=" << para->C << ", g=" << para->gamma << ", p=" << para->p << endl;
        }
        svm_model *model = svm_train(probs[i], para);
        ostringstream os;
        os << outname << "_" << i;
        svm_save_model(os.str().c_str(), model);
        svm_destroy_param(para);
        svm_free_and_destroy_model(&model);
    }
}
#endif
