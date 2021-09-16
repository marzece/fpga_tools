#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <complex.h>
#include <dlfcn.h>
#include "data_parser.c"

static PyObject *error;
static PyObject *NUM_CHANNELS;

static PyObject* _count_events(PyObject *self, PyObject *args) {
    PyObject* py_file_in;
    if (!PyArg_ParseTuple(args, "O", &py_file_in)) {
        PyErr_SetString(error, "Failed to interpret args");
        return NULL;
    }
    int fd = PyObject_AsFileDescriptor(py_file_in);
    //FILE* fin = fopen(fn, "rb");
    //int fd = open(fn, O_RDONLY);
    if(fd == -1) {
        PyErr_SetString(error, "Could not open given file");
        return NULL;
    }

    FILE* fin = fdopen(fd, "rb");
    if(!fin) {
        PyErr_SetString(error, "Error creating FILE from file descriptor");
        return NULL;
    }
    int count = count_events(fin);

    return Py_BuildValue("i", count);
}
static PyObject* _get_event_locations(PyObject *self, PyObject *args) {
    PyObject* py_file_in;
    long max_events=0;
    int i;
    if (!PyArg_ParseTuple(args, "O|i", &py_file_in, &max_events)) {
        PyErr_SetString(error, "Failed to interpret args");
        return NULL;
    }

    int fd = PyObject_AsFileDescriptor(py_file_in);
    //FILE* fin = fopen(fn, "rb");
    //int fd = open(fn, O_RDONLY);
    if(fd == -1) {
        PyErr_SetString(error, "Could not open given file");
        return NULL;
    }
    // Is there a better way to handle unsigned stuff?
    // Python's C interface doesn't seem to provide a way to have specifically unsigned args

    if(max_events < 0) {
        PyErr_SetString(error, "Max events cannot be negative!");
        return NULL;
    }

    FILE* fin = fdopen(fd, "rb");
    if(!fin) {
        PyErr_SetString(error, "Error creating FILE from file descriptor");
        return NULL;
    }
    EventIndex index = get_events_index(fin, (unsigned int)max_events);
    PyObject* locations = PyList_New(index.nevents);
    PyObject* lengths = PyList_New(index.nevents);
    for(i=0; i < index.nevents; i++) {
        PyList_SetItem(locations, i, PyLong_FromLong(index.locations[i]));
        PyList_SetItem(lengths, i, PyLong_FromLong(index.nsamples[i]));
    }

    free(index.locations);
    free(index.nsamples);
    PyObject* ret = Py_BuildValue("(O,O)", locations, lengths);
    return ret;

}

static PyObject* _get_event(PyObject *self, PyObject *args) {
    PyObject* py_file_in;
    int i;
    PyObject* result;
    long offset;
    int nsamples;
    uint16_t* samples = NULL;
    FILE* fin = NULL;
    if (!PyArg_ParseTuple(args, "Ol", &py_file_in, &offset)) {
        PyErr_SetString(error, "Failed to interpret args");
        return NULL;
    }

    int fd = PyObject_AsFileDescriptor(py_file_in);
    //FILE* fin = fopen(fn, "rb");
    //int fd = open(fn, O_RDONLY);
    if(fd == -1) {
        PyErr_SetString(error, "Could not open given file");
        return NULL;
    }

    if(offset < 0) {
        PyErr_SetString(error, "Second arguement is not a valid offset");
        return NULL;
    }

    fin = fdopen(fd, "rb");
    if(!fin) {
        PyErr_SetString(error, "Error creating FILE from file descriptor");
        return NULL;
    }
    nsamples = get_event(fin, offset, &samples);
    if(nsamples < 0) {
        PyErr_SetString(error, "Error reading event from file at given offset");
        return NULL;
    }

    // Convert from given uint16_t to python list
    result = PyList_New(nsamples*NCHANNELS);
    for(i=0; i<nsamples*NCHANNELS; i++) {
        PyList_SetItem(result, i, PyLong_FromUnsignedLong((unsigned long)samples[i]));
    }
    free(samples);
    samples=NULL;

    return result;
}

/*
static PyObject* _read_all_events(PyObject *self, PyObject *args) {
    PyObject* py_file_in;
    if (!PyArg_ParseTuple(args, "O", &py_file_in)) {
        PyErr_SetString(error, "Failed to interpret args");
        return NULL;
    }

    int fd = PyObject_AsFileDescriptor(py_file_in);
    //FILE* fin = fopen(fn, "rb");
    //int fd = open(fn, O_RDONLY);
    if(fd == -1) {
        PyErr_SetString(error, "Could not open given file");
        return NULL;
    }

    FILE* fin = fdopen(fd, "rb");
    if(!fin) {
        PyErr_SetString(error, "Error creating FILE from file descriptor");
        return NULL;

    }
*/
static PyMethodDef module_methods[] = {
    {"count_events", _count_events,  METH_VARARGS,
        "count_events(file)\n"
            "Count how many events are in the given file" },
    {"get_event_locations", _get_event_locations, METH_VARARGS,
        "get_event_locations(file)\n"
            "Get the index of each event in a file."
            "Returns a tuple of file locations and event lengths (i.e. sample count)" },
    {"get_event", _get_event, METH_VARARGS,
        "get_event(file, location)\n"
            "Returns an event located in file at offset location"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module_defn = {
    PyModuleDef_HEAD_INIT, 
    "fakernet_data_reader", 
    "",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit_fakernet_data_reader(void) {
    PyObject *m;
    m = PyModule_Create(&module_defn);
    if(!m) {return NULL;}
    error = PyErr_NewException("test.error", NULL, NULL);
    Py_INCREF(error);
    PyModule_AddObject(m, "error", error);

    NUM_CHANNELS = PyLong_FromLong(NCHANNELS);
    Py_INCREF(NUM_CHANNELS);
    PyModule_AddObject(m, "NUM_CHANNELS", NUM_CHANNELS);
    return m;
}
