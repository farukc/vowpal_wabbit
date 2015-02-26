/*
   Copyright (c) by respective owners including Yahoo!, Microsoft, and
   individual contributors. All rights reserved.  Released under a BSD (revised)
   license as described in the file LICENSE.
   */
#include "search_dep_parser.h"
#include "gd.h"

#define cdep cerr
#undef cdep
#define cdep if (1) {} else cerr
#define val_namespace 100 // valency and distance feature space
#define quad_namespace 101 // namespace for quadratic feature
#define cubic_namespace 102 // namespace for cubic feature
#define offset_const 344429

namespace DepParserTask         {  Search::search_task task = { "dep_parser", run, initialize, finish, NULL, NULL};  }

struct task_data {
  example *ex;
  bool no_quadratic_features;
  bool no_cubic_features;
  bool my_init_flag;
  int nfs;
  size_t root_label;
  size_t num_label;
  v_array<uint32_t> valid_actions;
  v_array<uint32_t> valid_labels;
  v_array<uint32_t> gold_action_reward;
  v_array<uint32_t> gold_heads; // gold labels
  v_array<uint32_t> gold_tags; // gold labels
  v_array<uint32_t> *children; // [0]:num_left_arcs, [1]:num_right_arcs; [2]: leftmost_arc, [3]: second_leftmost_arc, [4]:rightmost_arc, [5]: second_rightmost_arc
  v_array<uint32_t> stack; // stack for transition based parser
  v_array<uint32_t> heads; // output array
  v_array<uint32_t> tags; // output array
  v_array<uint32_t> temp;
  v_array<example *> ec_buf;
  vector<string> pairs;
  vector<string> triples;
};

namespace DepParserTask {
  using namespace Search;
  uint32_t max_label = 0;

  void initialize(Search::search& srn, size_t& num_actions, po::variables_map& vm) {
    task_data *data = new task_data();
    data->my_init_flag = false;
    data->ec_buf.resize(12, true);
	data->gold_action_reward.resize(4,true);
    data->children = new v_array<uint32_t>[6]; 

    for(size_t i = 0; i < 6; i++)
      data->children[i] = v_init<uint32_t>();

    data->valid_actions = v_init<uint32_t>();
    data->gold_heads = v_init<uint32_t>();
    data->stack = v_init<uint32_t>();
    data->heads = v_init<uint32_t>();
    data->ec_buf = v_init<example*>();
    data->temp = v_init<uint32_t>();


    srn.set_num_learners(3);
    srn.set_task_data<task_data>(data);
    po::options_description dparser_opts("dependency parser options");
    dparser_opts.add_options()
      ("root_label", po::value<size_t>(&(data->root_label))->default_value(8), "Ensure that there is only one root in each sentence")
      ("num_label", po::value<size_t>(&(data->num_label))->default_value(12), "Number of arc labels");
    srn.add_program_options(vm, dparser_opts);

    data->valid_labels.erase();
    for(size_t i=1; i<=data->num_label;i++)
      if(i!=data->root_label)
        data->valid_labels.push_back(i);

    data->ec_buf.resize(12,true);

    // setup entity and relation labels
    srn.set_options(AUTO_CONDITION_FEATURES);
  }

  void finish(Search::search& srn) {
    task_data *data = srn.get_task_data<task_data>();
    //    dealloc_example(CS::cs_label.delete_label, *(data->ex));
    data->valid_actions.delete_v();
    data->valid_labels.delete_v();
    data->gold_heads.delete_v();
    data->gold_tags.delete_v();
    data->stack.delete_v();
    data->heads.delete_v();
    data->tags.delete_v();
    data->ec_buf.delete_v();
    data->temp.delete_v();
    data->gold_action_reward.delete_v();

    for(size_t i=0; i<6; i++)
      data->children[i].delete_v();
    delete[] data->children;
    delete data;
  } // if we had task data, we'd want to free it here

  void inline add_feature(example *ex,  uint32_t idx, unsigned  char ns, size_t mask, size_t ss){
    feature f = {1.0f, (idx<<ss) & (uint32_t)mask};
    ex->atomics[(int)ns].push_back(f);
  }

  void inline reset_ex(example *ex){
    ex->num_features = 0;
    ex->total_sum_feat_sq = 0;
    for(unsigned char *ns = ex->indices.begin; ns!=ex->indices.end; ns++){
		if(*ns != constant_namespace){
	      	ex->atomics[(int)*ns].erase();
			ex->sum_feat_sq[(int)*ns] = 0;
		}
    }
  }
  // arc-hybrid System.
  uint32_t transition_hybrid(Search::search& srn, uint32_t a_id, uint32_t idx, uint32_t t_id) {
    task_data *data = srn.get_task_data<task_data>();
    v_array<uint32_t> &heads=data->heads, &stack=data->stack, &gold_heads=data->gold_heads, &gold_tags=data->gold_tags, &tags = data->tags;
    v_array<uint32_t> *children = data->children;
    switch(a_id) {
      //SHIFT
      case 1:
        stack.push_back(idx);
        return idx+1;

        //RIGHT
      case 2:
        heads[stack.last()] = stack[stack.size()-2];
        cdep << "make a right link" << stack[stack.size()-2] << " ====> " << (stack.last()) << endl;
        children[5][stack[stack.size()-2]]=children[4][stack[stack.size()-2]];
        children[4][stack[stack.size()-2]]=stack.last();
        children[1][stack[stack.size()-2]]++;
        tags[stack.last()] = t_id;
//		srn.loss((gold_heads[stack.last()] != heads[stack.last()])+(gold_tags[stack.last()] != t_id));
		if(gold_heads[stack.last()] != heads[stack.last()])
			srn.loss(2);
		else if (gold_tags[stack.last()] != t_id)
			srn.loss(1);
		else
			srn.loss(0);
        stack.pop();
        return idx;

        //LEFT
      case 3:
        heads[stack.last()] = idx;
        cdep << "make a left link" << stack.last() << "<==== " << idx << endl;
        children[3][idx]=children[2][idx];
        children[2][idx]=stack.last();
        children[0][idx]++;
        tags[stack.last()] = t_id;
//		srn.loss((gold_heads[stack.last()] != heads[stack.last()])+(gold_tags[stack.last()] != t_id));
		
		if(gold_heads[stack.last()] != heads[stack.last()])
			srn.loss(2);
		else if (gold_tags[stack.last()] != t_id)
			srn.loss(1);
		else
			srn.loss(0);

//        srn.loss((gold_heads[stack.last()] != heads[stack.last()]) + (gold_tags[stack.last()] != t_id));
        stack.pop();
        return idx;
    }
    cerr << "Unknown action (search_dep_parser.cc).";
    return idx;
  }

  void my_initialize(Search::search& srn, example *base_ex) {
    task_data *data = srn.get_task_data<task_data>();
    data->ex = alloc_examples(sizeof(base_ex[0].l.multi.label), 1);
    
	// setup feature template
    data->ex->indices.push_back(val_namespace);
    for(size_t i=0; i<13; i++)
      data->ex->indices.push_back((unsigned char)(i+'a'));
  }

  // Put features in place. This function needs to be very fast
  void extract_features(Search::search& srn, uint32_t idx,  vector<example*> &ec) {
    task_data *data = srn.get_task_data<task_data>();
    reset_ex(data->ex);
    size_t ss = srn.get_stride_shift(), mask = srn.get_mask();
    v_array<uint32_t> &stack = data->stack, &tags = data->tags, *children = data->children, &temp=data->temp;
    v_array<example*> &ec_buf = data->ec_buf;
    example &ex = *(data->ex);

    // be careful: indices in ec starts from 0, but i is starts from 1
    size_t n = ec.size();

    // feature based on top three examples in stack  ec_buf[0]: s1, ec_buf[1]: s2, ec_buf[2]: s3
    for(size_t i=0; i<3; i++)
      ec_buf[i] = (stack.size()>i && *(stack.end-(i+1))!=0) ? ec[*(stack.end-(i+1))-1] : 0;

    // features based on examples in string buffer ec_buf[3]: b1, ec_buf[4]: b2, ec_buf[5]: b3
    for(size_t i=3; i<6; i++)
      ec_buf[i] = (idx+(i-3)-1 < n) ? ec[idx+i-3-1] : 0;

    // features based on the children of the top element stack ec_buf[6]: sl1, ec_buf[7]: sl2, ec_buf[8]: sr1, ec_buf[9]: sr2;
    for(size_t i=6; i<10; i++)
      if (!stack.empty() && stack.last() != 0&& children[i-4][stack.last()]!=0)
        ec_buf[i] = ec[children[i-4][stack.last()]-1];

    // features based on leftmost children of the top element in bufer ec_buf[10]: bl1, ec_buf[11]: bl2
    for(size_t i=10; i<12; i++)
      ec_buf[i] = (idx <=n && children[i-8][idx]!=0)? ec[children[i-8][idx]-1] : 0;

	// feature based on the childrean of the second element in the stack ec_buf[13]
    ec_buf[12] = (stack.size()>1 && *(stack.end-2)!=0 && children[2][*(stack.end-2)]!=0)? ec[children[2][*(stack.end-2)]-1]:0;

    uint64_t v0;
    for(size_t i=0; i<13; i++) {
      if(!ec_buf[i]) continue;
      for (size_t j = 0;  j < ec_buf[i]->indices.size(); j++) {
		unsigned char fs = ec_buf[i]->indices[j];
        if(fs == constant_namespace) continue; // ignore constant_namespace
        uint32_t additional_offset = (uint32_t)(i*offset_const);
		unsigned char ts = i+'a';
        for(size_t k=0; k<ec_buf[i]->atomics[fs].size(); k++) {
            v0 = (ec_buf[i]->atomics[fs][k].weight_index>>ss);
          add_feature(&ex, (uint32_t) v0 + additional_offset, ts, mask, ss);
        }
      }
    }
    temp.resize(10,true);
    // distance
    temp[0] = stack.empty()? 0: (idx >n? 1: 2+min(5, idx - stack.last()));

    // #left child of top item in stack
    temp[1] = stack.empty()? 1: 1+min(5, children[0][stack.last()]);

    // #right child of top item in stack
    temp[2] = stack.empty()? 1: 1+min(5, children[1][stack.last()]);

    // #left child of rightmost item in buf
    temp[3] = idx>n? 1: 1+min(5 , children[0][idx]);
	
    for(size_t i=4; i<8; i++)
      if (!stack.empty() && children[i-2][stack.last()]!=0)
        temp[i] = tags[children[i-2][stack.last()]];
	  else
  	    temp[i] = 15;

    // features based on leftmost children of the top element in bufer
    // ec_buf[10]: bl1, ec_buf[11]: bl2
    for(size_t i=8; i<10; i++)
        temp[i] = (idx <=n && children[i-6][idx]!=0)? tags[children[i-6][idx]] : 15;
	

    size_t additional_offset = val_namespace*offset_const; 
    for(int j=0; j< 10;j++) {
 	  additional_offset += j* 1023;
      add_feature(&ex, temp[j]+ additional_offset , val_namespace, mask, ss);
	}
    // action history
	
    size_t count=0;
    for (unsigned char* ns = data->ex->indices.begin; ns != data->ex->indices.end; ns++) {
      data->ex->sum_feat_sq[(int)*ns] = (float) data->ex->atomics[(int)*ns].size();
      count+= data->ex->atomics[(int)*ns].size();
    }
    data->ex->num_features = count;
    data->ex->total_sum_feat_sq = (float) count;
  }

  void get_valid_actions(v_array<uint32_t> & valid_action, uint32_t idx, uint32_t n, uint32_t stack_depth, uint32_t state) {
    valid_action.erase();
    // SHIFT
    if(idx<=n)
      valid_action.push_back(1);

    // RIGHT
    if(stack_depth >=2)
      valid_action.push_back(2);

    // LEFT
    if(stack_depth >=1 && state!=0 && idx<=n)
      valid_action.push_back(3);
  }

  bool is_valid(uint32_t action, v_array<uint32_t> valid_actions) {
    for(size_t i=0; i< valid_actions.size(); i++)
      if(valid_actions[i] == action)
        return true;
    return false;
  }

  size_t get_gold_actions(Search::search &srn, uint32_t idx, uint32_t n){
    task_data *data = srn.get_task_data<task_data>();
    v_array<uint32_t> &gold_action_reward = data->gold_action_reward, &stack = data->stack, &gold_heads=data->gold_heads, &valid_actions=data->valid_actions;

    // gold = SHIFT
    if (is_valid(1,valid_actions) &&( stack.empty() || gold_heads[idx] == stack.last()))
		return 1;

    // gold = LEFT
    if (is_valid(3,valid_actions) && gold_heads[stack.last()] == idx)
		return 3;

	for(size_t i = 1; i<= 3; i++)
		gold_action_reward[i] = 500;

    // dependency with SHIFT
    for(uint32_t i = 0; i<stack.size()-1; i++)
   	  if(idx <=n && (gold_heads[stack[i]] == idx || gold_heads[idx] == stack[i]))
		  gold_action_reward[1] -= 1;

 	if(stack.size()>0 && gold_heads[stack.last()] == idx)
	  gold_action_reward[1] -= 1;

    // dependency with left and right
    for(uint32_t i = idx+1; i<=n; i++)
   	  if(gold_heads[i] == stack.last()|| gold_heads[stack.last()] == i) {
		  gold_action_reward[3] -=1;
	  }
	if(gold_heads[idx] == stack.last())
		  gold_action_reward[3] -=1;

	if(stack.size()>=2 && gold_heads[stack.last()] == stack[stack.size()-2])
		gold_action_reward[3] -= 1;

	if(gold_heads[stack.last()] >=idx)
		gold_action_reward[2] -=1;

	for(uint32_t i = idx; i<=n; i++)
   	  if(gold_heads[i] == stack.last())
          gold_action_reward[2] -=1;

	// break the tie between left and right


	// remove invalid actions
	for(size_t i=1; i<=3; i++)
      if (!is_valid(i,valid_actions))
		  gold_action_reward[i] = 0;

	// return the best action
	size_t best_action = 1;
	for(size_t i=1; i<=3; i++)
		if(gold_action_reward[i] >= gold_action_reward[best_action])
			best_action= i;
	return best_action;
  }

  void run(Search::search& srn, vector<example*>& ec) {
    cdep << "start structured predict"<<endl;
    task_data *data = srn.get_task_data<task_data>();

    v_array<uint32_t> &stack=data->stack, &gold_heads=data->gold_heads, &valid_actions=data->valid_actions, &heads=data->heads, &gold_tags=data->gold_tags, &tags=data->tags, &valid_labels=data->valid_labels;
    uint32_t n = (uint32_t) ec.size();



    // initialization
    if(!data->my_init_flag) {
      my_initialize(srn, ec[0]);
      data->my_init_flag = true;
    }    
    heads.resize(ec.size()+1, true);
    tags.resize(ec.size()+1, true);
    gold_heads.erase();
    gold_heads.push_back(0);
    gold_tags.erase();
    gold_tags.push_back(0);
    for(size_t i=0; i<ec.size(); i++) {
      uint32_t label = ec[i]->l.multi.label;
      gold_heads.push_back((label & 255) -1);
      gold_tags.push_back(label >>8);
      heads[i+1] = 0;
      tags[i+1] = -1;
    }
    stack.erase();
   	stack.push_back((data->root_label==0)?0:1);
    uint32_t idx = ((data->root_label==0)?1:2);
    for(size_t i=0; i<6; i++){
      data->children[i].resize(ec.size()+1, true);
      for(size_t j=0; j<ec.size()+1; j++)
        data->children[i][j] = 0;
    }

    int count=0;
    cdep << "start decoding"<<endl;
    while(stack.size()>1 || idx <= n){
      if(srn.predictNeedsExample())
        extract_features(srn, idx, ec);
      get_valid_actions(valid_actions, idx, n, (uint32_t) stack.size(), stack.last());
      uint32_t gold_action = get_gold_actions(srn, idx, n);
	  if(gold_action==0) gold_action = valid_actions[0];

      // Predict the next action {SHIFT, REDUCE_LEFT, REDUCE_RIGHT}
      uint32_t a_id= Search::predictor(srn, (ptag) 0).set_input(*(data->ex)).set_oracle(gold_action).set_allowed(valid_actions).set_condition_range(count, srn.get_history_length(), 'p').set_learner_id(0).predict();
      count++;
      uint32_t t_id = 0;
      if(a_id ==2 || a_id == 3){
	  	uint32_t gold_label = gold_tags[stack.last()];
        t_id= Search::predictor(srn, (ptag) count+1).set_input(*(data->ex)).set_oracle(gold_label).set_allowed(valid_labels).set_condition_range(count, srn.get_history_length(), 'p').set_learner_id(a_id-1).predict();
        count++;
      }
      idx = transition_hybrid(srn, a_id, idx, t_id);
    }
//	if(data->root_label!=0){
	    heads[stack.last()] = 0;
	    tags[stack.last()] = data->root_label;
//	}
//	else

    cdep << "root link to the last element in stack" <<  "root ====> " << (stack.last()) << endl;
    srn.loss((gold_heads[stack.last()] != heads[stack.last()]));
    if (srn.output().good())
      for(size_t i=1; i<=n; i++) {
        cdep << heads[i] << " ";
        srn.output() << (heads[i])<<":"<<tags[i] << endl;
      }
    cdep << "end structured predict"<<endl;
  }
}
