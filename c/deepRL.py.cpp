/*
 * deepRL
 */

#include "deepRL.h"

#include "pyTorch.h"
#include "pyTensor.h"


#ifdef USE_PYTHON

//-------------------------------------------------------------------------------
bool deepRL::scriptingLoaded = false;
//-------------------------------------------------------------------------------


// constructor
deepRL::deepRL()
{	
	//mNewEpisode = true;
	mNumInputs  = 0;
	mNumActions = 0;
	mModuleName = "RL";
	mModuleObj  = NULL;
	
	mActionTensor = NULL;
	mRewardTensor = NULL;

	for( uint32_t n=0; n < NUM_FUNCTIONS; n++ )
	{
		mFunction[n] 	  = NULL;
		mFunctionArgs[n] = NULL;
	}
}


// destructor
deepRL::~deepRL()
{

}


// Create
deepRL* deepRL::Create( uint32_t numInputs, uint32_t numActions, const char* module, const char* nextAction, const char* nextReward )
{
	if( !module || numInputs == 0 || numActions == 0 || !module || !nextAction || !nextReward )
		return NULL;

	return Create(numInputs, 1, numActions, module, nextAction, nextReward);
}


// Create
deepRL* deepRL::Create( uint32_t width, uint32_t height, uint32_t numActions, const char* module, const char* nextAction, const char* nextReward )
{
	if( !module || width == 0 || height == 0 || numActions == 0 || !module || !nextAction || !nextReward )
		return NULL;

	// create new object
	deepRL* rl = new deepRL();

	if( !rl )
		return NULL;

	const uint32_t numInputs = width * height;

	// format argument strings
	char inputsStr[32];
	char actionStr[32];
	
	sprintf(inputsStr, "--inputs=%u", numInputs);
	sprintf(actionStr, "--actions=%u", numActions);

	// set python command line
	int py_argc = 3;
	char* py_argv[3];

	py_argv[0] = (char*)module;
	py_argv[1] = inputsStr;
	py_argv[2] = actionStr;
	
	// load python module
	if( !rl->LoadModule(module, py_argc, py_argv) )
		return NULL;

	// retrieve python RL functions
	PyObject* actionFunc = PyObject_GetAttrString((PyObject*)rl->mModuleObj, nextAction);
	PyObject* rewardFunc = PyObject_GetAttrString((PyObject*)rl->mModuleObj, nextReward);

	if( !actionFunc )
	{
		if (PyErr_Occurred())
			PyErr_Print();

		printf("[deepRL]  failed to find function %s() in Python module '%s'\n", nextAction, module);
		return NULL;
	}

	if( !rewardFunc )
	{
		if (PyErr_Occurred())
			PyErr_Print();

		printf("[deepRL]  failed to find function %s() in Python module '%s'\n", nextReward, module);
		return NULL;
	}

	// check that the retrieved functions are actually callable
	if( !PyCallable_Check(actionFunc) )
	{
		if (PyErr_Occurred())
			PyErr_Print();

		printf("[deepRL]  %s() from Python module '%s' is not a callable function\n", nextAction, module);
		return NULL;
	}

	if( !PyCallable_Check(rewardFunc) )
	{
		if (PyErr_Occurred())
			PyErr_Print();

		printf("[deepRL]  %s() from Python module '%s' is not a callable function\n", nextReward, module);
		return NULL;
	}

	rl->mFunction[ACTION_FUNCTION] = (void*)actionFunc;
	rl->mFunction[REWARD_FUNCTION] = (void*)rewardFunc;
	
	rl->mFunctionName[ACTION_FUNCTION] = nextAction;
	rl->mFunctionName[REWARD_FUNCTION] = nextReward;

	// allocate function arguments
	PyObject* actionArgs = PyTuple_New(1);
	PyObject* rewardArgs = PyTuple_New(2);

	if( !actionArgs || !rewardArgs )
	{
		printf("[deepRL]  failed to allocated PyTuple for function arguments\n");
		return NULL;
	}

	rl->mFunctionArgs[ACTION_FUNCTION] = (void*)actionArgs;
	rl->mFunctionArgs[REWARD_FUNCTION] = (void*)rewardArgs;
	
	return rl;
}


// LoadInterpreter
bool deepRL::LoadInterpreter()
{
	if( scriptingLoaded )
		return true;

	Py_Initialize();
	
	scriptingLoaded = true;
	return true;
}


// LoadModule
bool deepRL::LoadModule( const char* module )
{
	int argc = 0;
	return LoadModule(module, argc, NULL);
}


// LoadModule
bool deepRL::LoadModule( const char* module, int argc, char** argv )
{
	if( !LoadInterpreter() )
		return false;

	if( argc > 0 ) 
		PySys_SetArgv(argc, argv);	

	PyObject* pyModuleName = PyString_FromString(module);

	if( !pyModuleName )
		return false;

	PyObject* pyModule = PyImport_Import(pyModuleName);
	Py_DECREF(pyModuleName);

	if( !pyModule )
	{
		PyErr_Print();
		printf("failed to load python module '%s'\n", module);
		return false;
	}

	mModuleName = module;
	mModuleObj  = (void*)pyModule;
	return true;
}


// NextAction
bool deepRL::NextAction( Tensor* state, int* action )
{
	if( !state || !action )
		return false;

	// setup arguments to action function
	PyObject* pArgs = PyTuple_New(1); //(PyObject*)mFunctionArgs[ACTION_FUNCTION];

	PyTuple_SetItem(pArgs, 0, state->pyTensorGPU);
	
	// call action function
	PyObject* pValue = PyObject_CallObject((PyObject*)mFunction[ACTION_FUNCTION], pArgs);

	//Py_DECREF(pArgs);

	// check return value
	if( pValue != NULL )
	{
		//printf("[deepRL]  result of %s(): %ld\n", mFunctionName[ACTION_FUNCTION].c_str(), PyInt_AsLong(pValue));
		*action = (int)PyInt_AsLong(pValue);		
		Py_DECREF(pValue);
	}
	else
	{
		PyErr_Print();
		printf("[deepRL]  call to %s() failed\n", mFunctionName[ACTION_FUNCTION].c_str());
		return false;
	}

	return true;
}


// NextReward
bool deepRL::NextReward( float reward, bool end_episode )
{
	// setup arguments to action function
	PyObject* pArgs = PyTuple_New(2); //(PyObject*)mFunctionArgs[REWARD_FUNCTION];

	//PyTuple_SetItem(pArgs, 0, state->pyTensorGPU);
	PyTuple_SetItem(pArgs, 0, PyFloat_FromDouble(reward));
	PyTuple_SetItem(pArgs, 1, PyBool_FromLong(/*mNewEpisode*/end_episode ? 1 : 0));

	//mNewEpisode = false;	// reset new_ep flag

	// call reward/training function
	PyObject* pValue = PyObject_CallObject((PyObject*)mFunction[REWARD_FUNCTION], pArgs);

	//Py_DECREF(pArgs);		// this invalidates the tensors

	// check return value
	/*if( pValue != NULL )
	{
		//printf("[deepRL]  result of %s(): %ld\n", mRewardFunctionName.c_str(), PyInt_AsLong(pValue));
		*action = (int)PyInt_AsLong(pValue);		
		Py_DECREF(pValue);
	}
	else
	{
		PyErr_Print();
		printf("[deepRL]  call to %s() failed\n", mFunctionName[REWARD_FUNCTION].c_str());
		return false;
	}*/

	if( !pValue )
	{
		PyErr_Print();
		printf("[deepRL]  call to %s() failed\n", mFunctionName[REWARD_FUNCTION].c_str());
		return false;
	}

	return true;
}


// NextEpisode
/*void deepRL::NextEpisode()
{
	mNewEpisode = true;
}*/

#endif
