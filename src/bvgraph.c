/*
 * David Gleich
 * Copyright, Stanford University, 2007
 * 17 May 2007
 */

/**
 * @file bvgraph.c
 * Implement all the routines to work with bvgraph files in the bvg library.
 * @author David Gleich
 * @date 17 May 2007
 * @brief implementation the routines in bvgraph.h
 *
 * @version
 *
 *  01-24-2008: Added function to initialize structure memory to fix a 
 *              segfault on the 32-bit version.
 */

#include "bvgraph_internal.h"
#include "bvgraph_inline_io.h"
#include "eflist.h"
#include <inttypes.h>
#include <math.h>

/**
 * Define all the error codes
 */

const int bvgraph_call_out_of_memory = -1;          ///< error code for call out of memory
const int bvgraph_call_io_error = -2;               ///< error code for io error
const int bvgraph_call_unsupported = -3;            ///< error code for unsupported call
const int bvgraph_load_error_filename_too_long = 11;///< error code for file name too long
const int bvgraph_load_error_buffer_too_small = 12; ///< error code for buffer too small
const int bvgraph_property_file_error = 21;         ///< error code for property file 
const int bvgraph_unsupported_version = 22;         ///< error code for unsupported version
const int bvgraph_property_file_compression_flag_error = 23;  ///< error code for property file compression flag
const int bvgraph_vertex_out_of_range = 31;         ///< error code for vertex out of range
const int bvgraph_requires_offsets = 32;            ///< error code for missing offsets
const int bvgraph_unsupported_coding = 33;          ///< error code for unsupported coding method

/**
 * This function sets the default options in a graph
 */

static void set_defaults(bvgraph *g)
{
    g->zeta_k = 3;
    g->window_size = 7;
    g->min_interval_length = 3;
    g->max_ref_count = 3;
}

/**
 * Create a new bvgraph in the memory.
 * @return A pointer to the newly created bvgraph in the memory.
 */

bvgraph *bvgraph_new(void)
{
    bvgraph *g;
    g = malloc(sizeof(bvgraph));

    return (g);
}

/**
 * Free a bvgraph in the memory.
 * @param[in] g a pointer to the bvgraph in the memory
 */

void bvgraph_free(bvgraph *g)
{
    free(g);
    g = NULL;
}

/**
 * Load the metadata associated with a bvgraph.  Largely, this involves
 * just parsing the properties files.
 *
 * @param[in] g a newly created bvgraph structure
 * @param[in] filename the base filename for a set of bvgraph files, 
 * so filename.graph and filename.properties must exist.
 * @param[in] offset_step controls how many offsets are loaded, 
 * if offset_step = -1, then the graph file isn't loaded into memory
 * if offset_step = 0, then the graph file is loaded, but no offsets
 * no other values are supported at the moment.
 * @return 0 if successful;
 * bvgraph_load_error_filename_too_long - indicates the filename was too long
 *
 * @code
 * bvgraph g; bvgraph_load(&b, "cnr-2000", 0);
 * @endcode
 */
int bvgraph_load(bvgraph* g, const char *filename, unsigned int filenamelen, int offset_step)
{
    // this call will treat all the memory as internal
    return bvgraph_load_external(g, filename, filenamelen, offset_step, NULL, 0, NULL, 0);
}  

/**
 * Load a graph file but using a set of externally provided buffers 
 * for the data.  This might be useful in the case that you want to managed
 * memory for the graph independently of this function.  
 *
 * To maintain the graph, only the bvgraph structure, and the two
 * external ararys: gmemory and offsets are required.  Consequently,
 * a bvgraph structure can always be patched together out of these
 * functions.
 *
 * One common way of using this function would be to first
 * load a graph on the disk (bvgraph_load(...,offset_step=-1))
 * and then call bvgraph_required_memory with an alternative
 * offset step.
 *
 * @param[in] g a newly created bvgraph structure
 * @param[in] filename the base filename for a set of bvgraph files, 
 * so filename.graph and filename.properties must exist.
 * @param[in] offset_step controls how many offsets are loaded, 
 * if offset_step = -1, then the graph file isn't loaded into memory
 * if offset_step = 0, then the graph file is loaded, but no offsets
 * no other values are supported at the moment.
 * @param[in] gmemory an arry of size gmemsize for the graph 
 * (if NULL, then this parameter is treated as internal memory)
 * @param[in] gmemsize the size of the gmemory block
 * @param[in] offsets an array of offsets
 * (if NULL, then this parameter is treated as internal memory)
 * @param[in] offsetssize the number of offsets
 * @return 0 if successful;
 * bvgraph_load_error_filename_too_long - indicates the filename was too long
 */
int bvgraph_load_external(bvgraph *g,
                          const char *filename, unsigned int filenamelen, int offset_step,
                          unsigned char *gmemory, size_t gmemsize,
                          unsigned long long* offsets, int offsetssize)
{
    int rval = 0;
    // the offset_step can be ANY value now!
    //assert(offset_step == 0 || offset_step == -1 || offset_step == 1);

    if (filenamelen > BVGRAPH_MAX_FILENAME_SIZE-1) { 
        return bvgraph_load_error_filename_too_long;
    }
    
    memset(g,0,sizeof(bvgraph));

    strncpy(g->filename, filename, filenamelen);
    g->filename[filenamelen] = '\0';
    g->filenamelen = filenamelen;

    g->offset_step = offset_step;
    set_defaults(g);

    rval = parse_properties(g);
    // check for any errors
    if (rval != 0) { return rval; }

    // continue processing
    if (offset_step >= 0) 
    {
        //modified 082911
        // in this case, we must load the graph
        // file into memory

        // first get the filesize
        unsigned long long graphfilesize;
        char *gfilename = strappend(g->filename, g->filenamelen, ".graph", 6);
        rval = fsize(gfilename, &graphfilesize);
        free(gfilename);
        if (rval) { 
            return bvgraph_call_io_error;
        }

        if (gmemory != NULL) {
            // they want to use their own memory, make sure
            // they allocated enough!
            if (gmemsize < graphfilesize) {
                return bvgraph_load_error_buffer_too_small;
            }
            g->memory = gmemory;
            g->memory_size = gmemsize;
            g->memory_external = 1;
        } else {
            // we have to allocate the memory ourselves
            g->memory_size = (size_t)graphfilesize;
            g->memory = malloc(sizeof(unsigned char)*g->memory_size);
            if (!g->memory) {
                return bvgraph_call_out_of_memory;
            }
            g->memory_external = 0;
        }

        // now read the file
        gfilename = strappend(g->filename, g->filenamelen, ".graph", 6);
        {
            size_t bytesread = 0;
            FILE *gfile = fopen(gfilename, "rb");
            free(gfilename);
            if (!gfile) {
                return bvgraph_call_io_error;
            }
            bytesread = fread(g->memory, 1, g->memory_size, gfile);
            if (bytesread != graphfilesize) {
                return bvgraph_call_io_error;
            }
            fclose(gfile);
        }
        // we now have the graph in memory!
        g->use_ef = 0;

        if (offset_step == 1) {        //modified 082911
            if (offsets != NULL) {
                g->offsets = offsets;
                g->offsets_external = 1;
            } else {
                // we have to allocate the memory ourselves
                g->offsets = (unsigned long long*) malloc(sizeof(unsigned long long)*g->n);
                g->offsets_external = 0;
            }
            // now read the file
            rval = load_offset_from_file(g);
            if (rval) {
                load_offset_online(g);
            }            
        }
        else if (offset_step == 2) {
            // for test purpose, only generate efcode for offset
            g->use_ef = 1;
            g->offsets_external = 1;
            rval = build_efcode(g, 0);
            if (!rval) {
                build_efcode(g, 1);
            }
        }
        else if (offset_step > 2){ 
            // offset_step > 2, the size of memory that a user is willing to allocate
            int64_t offset_in_mem = g->n * 8;  // amount of memory in bytes
            if (offset_in_mem > offset_step) {
                // use EF code to ensure minimum memory
                g->use_ef = 1;
                g->offsets_external = 1;
                printf("The memory required for offsets is larger than %d MB.\nLoading with EF code instead.\n", offset_step);
                rval = build_efcode(g, 0);
                if (!rval) {
                    build_efcode(g, 1);
                }
            }
            else {
                // load the offset
                g->use_ef = 0;
                rval = load_offset_from_file(g);
                if (rval) {
                    load_offset_online(g);
                }
                g->offsets_external = 0;
            }
            
        }
    }
    else // TODO: add semantics here for offset_step < 0
    {
        if (offset_step == -1) { // graph on disk and no offset
            g->memory_size = 0;
        }
        else {
            // leave the graph on disk
            // use EF code for the offsets
            g->memory_size = 0;
            g->use_ef = 1;
            g->offsets_external = 1;
            rval = build_efcode(g, 0);
            if (!rval) {
                build_efcode(g, 1);
            }
        }
    }
     
    // presently the othercases are not supported, so we don't have to
    // worry about them!

    return (0);
}

/**
 * Close a bvgraph object.  This operation will free any associated memory.
 * However, it will leave any existing iterators in an zombie state.  These
 * iterators should be closed before closing the graph, but this is only
 * recommended to avoid bugs.
 *
 * @todo Implement a "valid" function that all the iterators would check?
 *
 * @param[in] g the graph
 * @return 0 on success
 */
int bvgraph_close(bvgraph* g)
{
    if (!g->memory_external) { free(g->memory); }
    if (!g->offsets_external) { free(g->offsets); }
    memset(g, 0, sizeof(bvgraph));
    if (g->use_ef) {
        eflist_free(&(g->ef));
    }

    return (0);
}

// This function should be invoked after loading the bvgraph with offset_step = -1;
// otherwise the behavior is not defined.
static size_t eflist_size(bvgraph *g)
{
    size_t rval = 0;
    uint64_t build_last = (uint64_t)(g->bits_per_link * g->m);
    uint64_t u = build_last + 1;
    int s = g->n == 0 ? 0 : (int)(log2(u / g->n));
    rval += (s * g->n + 63) / 64 * 8;    // number of bytes for lower bits array
    int64_t upper_length = g->n + (build_last >> s);
    rval += (upper_length + 63) / 64 * 8; // number of bytes for upper bits array
    int window = upper_length == 0 ? 1 : (int)((g->n * MAX_ONES_PER_INVENTORY + upper_length - 1) / upper_length);
    int log2_ones_per_inventory = (int)(floor(log2(window)));
    int ones_per_inventory = 1 << log2_ones_per_inventory;
    int inventory_size = (int)((g->n + ones_per_inventory - 1) / ones_per_inventory);
    rval += inventory_size * 8;   // number of bytes for inventory array
    rval += DEFAULT_SPILL_SIZE * 8; // number of bytes for spill
    return rval;
}

/**
 * Compute the memory required to load a bvgraph into memory
 * with the desired offset_step.  
 *
 * The best way to use this function is to load the bvgraph
 * with offset_step = -1, and then call this function to determine
 * how much additional memory is required.  
 *
 * @param[in] g the graph
 * @param[in] offset_step the new offset_step value
 * @param[out] gbuf the size of the graph buffer
 * @param[out] offsetbuf the size of the offset buffer
 * @param[out] efbuf the size of the eflist buffer
 * @return 0 on success
 */
int bvgraph_required_memory(bvgraph *g, int offset_step, size_t *gbuf, size_t *offsetbuf, size_t *efbuf)
{
    if (offset_step <= -1) {
        if (gbuf) { *gbuf = 0; }
        if (offsetbuf) { *offsetbuf = 0; }
        if (efbuf) { 
            *efbuf = 0; 
            if (offset_step < -1) {
                // compute the buffer size for eflist
                *efbuf = eflist_size(g);
            }
        }
    }
    else {
        unsigned long long graphfilesize;
        char *gfilename = strappend(g->filename, g->filenamelen, ".graph", 6);
        int rval = fsize(gfilename, &graphfilesize);
        free(gfilename);
        if (rval) { 
            return bvgraph_call_io_error;
        }

        if (gbuf) { *gbuf = (size_t)graphfilesize; }
        // always set the offsetbuf here even if we are about to change
        // it.
        if (offsetbuf) { *offsetbuf = 0; }
        
        if (efbuf) { *efbuf = 0; }

        if (offset_step == 1) {
            if (offsetbuf) { *offsetbuf = sizeof(unsigned long long)*g->n; }
        }
        else if (offset_step == 2) {
            *efbuf = eflist_size(g);
        }
        else if (offset_step > 2) {
            // check if user allowed memory is enough for offset
            if (offset_step * (1L << 20) >= sizeof(unsigned long long)*g->n) {
                *offsetbuf = sizeof(unsigned long long)*g->n;
            }
            else {
                *efbuf = eflist_size(g);
            }
        }
    }
/*    else {
        return bvgraph_call_unsupported;
    }*/

    return (0);
}

/** Access the outdegree for a given node.
 *
 * For a random access graph, this method provides a thread-safe means of 
 * getting the outdegree for a given node.  If you are going to make
 * many sequential outdegree calls from a single thread, the 
 * random_access_iterator structure is much more efficient.
 *
 * To use this method, the graph must be loaded with offsets.
 *
 * @param[in] g the bvgraph structure with offsets loaded
 * @param[in] x the node
 * @param[out] d the outdegree
 * @return 0 on success
 */

int bvgraph_outdegree(bvgraph *g, int64_t x, uint64_t *d) 
{
    bvgraph_random_iterator ri;
    int rval = bvgraph_random_access_iterator(g, &ri);
    if (rval == 0) {
        rval = bvgraph_random_outdegree(&ri, x, d);
    }
    bvgraph_random_free(&ri);
    return (rval);
}

/** Access the successors for a given node.
 *
 * For a random access graph, this method provides a thread-safe means of 
 * getting the outdegree for a given node.  If you are going to make
 * many sequential outdegree calls from a single thread, the 
 * random_access_iterator structure is much more efficient.
 *
 * To use this method, the graph must be loaded with offsets.
 *
 * @param[in] g the bvgraph structure with offsets loaded
 * @param[in] x the node
 * @param[out] start the starting point of successor list
 * @param[out] length the length of successor list (degree)
 * @return 0 on success
 */

int bvgraph_successors(bvgraph *g, int64_t x, int64_t** start, uint64_t *length) 
{
    bvgraph_random_iterator ri;
    int rval = bvgraph_random_access_iterator(g, &ri);
    if (rval == 0) {
        bvgraph_random_successors(&ri, x, start, length);
        //bvgraph_random_free(&ri);
        return (0);
    } else { 
        return (rval); 
    }
}


/**
 * Return an error string associated with an error code.
 * 
 * @param[in] code the error code
 * @return the pointer to a string associated with an error code.
 */
const char* bvgraph_error_string(int code)
{
    if (code == bvgraph_call_out_of_memory) {
        return "malloc error --- probably out of memory";
    }
    else if (code == bvgraph_call_io_error) {
        return "io error --- probably file not found";
    }
    else if (code == bvgraph_call_unsupported) {
        return "the call tried to perform an unsupported operation";
    }
    else if (code == bvgraph_load_error_filename_too_long) {
        return "filename too long to store";
    }
    else if (code == bvgraph_load_error_buffer_too_small) {
        return "one of the provided buffers was too small";
    }
    else if (code == bvgraph_property_file_error) {
        return "the property file is not a valid property file format";
    }
    else if (code == bvgraph_property_file_compression_flag_error) {
        return "the property file contained an unknown compression flag";
    }
    else if (code == bvgraph_call_io_error) {
        return "the file version is not supported";
    }
    else if (code == bvgraph_vertex_out_of_range){
        return "vertex is out of range";
    }
    else if (code == bvgraph_requires_offsets){
        return "offsets are required";
    }
    else if (code == bvgraph_unsupported_coding){
        return "coding unsupported";
    }
    else if (code == 0) {
        return "the call succeeded";
    }
    else {
        return "unknown error";
    }
}


/**
 * This function load the offset array from file on disk.
 * 
 * @param[in] g the graph
 * @return 0 on success
 */
int load_offset_from_file(bvgraph *g)
{
    char *ofilename = strappend(g->filename, g->filenamelen, ".offsets", 8);
    bitfile bf;
    long long off = 0;
    int64_t i;
    g->offsets = (unsigned long long*)malloc(g->n*sizeof(unsigned long long));
    FILE *ofile = fopen(ofilename, "rb");
    if (ofile) {
        int rval = bitfile_open(ofile, &bf);
        if (rval) {
            return bvgraph_call_io_error;
        }
        for (i = 0; i < g->n; i++){
            off = read_offset(g, &bf) + off;
            g->offsets[i] = off;
        }
        bitfile_close(&bf);
        fclose(ofile);
        return 0;  // success
    }
        
    return 1; // failure
}

/**
 * This function creates the offset array from the graph in the memory.
 * 
 * @param[in] g the graph
 * @return 0 on success
 */
int load_offset_online(bvgraph *g)
{
    // need to build the offsets
    bvgraph_iterator git;
    int rval = bvgraph_nonzero_iterator(g, &git);
    if (rval) { return rval; }
    g->offsets[0] = 0;
    for (; bvgraph_iterator_valid(&git); bvgraph_iterator_next(&git)) {
        if (git.curr+1 < g->n) {
            g->offsets[git.curr+1] = bitfile_tell(&git.bf);
        }
    }
    bvgraph_iterator_free(&git);
    return 0;
}

/**
 * This function computes the EF code from the offset file on disk.
 * 
 * @param[in] g the graph
 * @return 0 on success
 */
int load_efcode_from_file(bvgraph *g)
{
    elias_fano_list *ef = &(g->ef);
    simple_select *sel = &((g->ef).sel);
    uint64_t n = g -> n;
    bitfile bf;
    long long off = 0;
    char *ofilename = strappend(g->filename, g->filenamelen, ".offsets", 8);
    FILE *ofile = fopen(ofilename, "rb");
    int64_t i; //last_elm, 
    int rval;
    if (ofile) { //if offsets file exists
        rval = bitfile_open(ofile, &bf);
        if (rval) {
            return bvgraph_call_io_error;
        }
        // here we build the estimate of last element from the property file
        // by g->bits_per_link * g->m, this value is larger than the last element in the array
        uint64_t build_last = (uint64_t)(g->bits_per_link * g->m);
        uint64_t u = build_last + 1;
        int s = n == 0 ? 0 : (int)(log2(u / n));
        ef->s = s;
        bit_array_create(&(ef->lower), s, n);
        int64_t upper_length = n + (build_last >> s);
        sel->bitarraylen = upper_length;
        bit_array_create(&(ef->upper), -1, (upper_length + 63) / 64);
        rval = bitfile_open(ofile, &bf);
        if (rval) {
            return bvgraph_call_io_error;
        }
        off = 0;
        for (i = 0; i < g->n; i ++) {
            off = read_offset(g, &bf) + off;
            eflist_add(ef, off);
        }
        bitfile_close(&bf);
        fclose(ofile);
        return 0;  // success
    }
    return 1;  //failure
}


/**
 * This function computes the EF code from the graph loaded in the memory.
 *
 * @param[in] g the graph
 * @return 0 on success
 */
int load_efcode_online(bvgraph *g)
{
    elias_fano_list *ef = &(g->ef);
    simple_select *sel = &((g->ef).sel);
    uint64_t n = g -> n;
    bvgraph_iterator git;
    int r = bvgraph_nonzero_iterator(g, &git);
    int64_t last_elm;
    if (r) { return r; }
    last_elm = 0;
    // estimate the last element from the propery file by
    // g->bits_per_link * g->m
    uint64_t build_last = (uint64_t)(g->bits_per_link * g->m);
    uint64_t u = build_last + 1; // upper bound on entries
    int s = n == 0 ? 0 : (int)(log2(u / n));
    ef->s = s;
    bit_array_create(&(ef->lower), s, n);
    int64_t upper_length = n + (build_last >> s);
    sel->bitarraylen = upper_length; 
    bit_array_create(&(ef->upper), -1, (upper_length + 63) / 64);
    r = bvgraph_nonzero_iterator(g, &git);
    if (r) { return r; }
    eflist_add(ef, 0);
    for (; bvgraph_iterator_valid(&git); bvgraph_iterator_next(&git)) {
        if (git.curr + 1 < g->n) {
            last_elm = bitfile_tell(&git.bf);
            eflist_add(ef, last_elm);
        }
    }
    bvgraph_iterator_free(&git);
    return 0;
}

/**
 * The function builds the Elias-Fano representation of a monotonously non-decreasing sequence.
 * EF coding is used to encode a monotone nondecreasing natrual number 
 * sequence x0, x1, ..., x_{n-1}. Suppose all the numbers are smaller 
 * than an upper bound u. Then EF representation can encode the sequence 
 * using at most 2 + log(u / n) bits. A typical usage: suppose we have 
 * a list of pointers, which point to records in a large file. Instead 
 * of using many bits to express the length of the file for the pointer, 
 * EF coding makes it possible to use roughly equal length of bits to 
 * represent pointers. The length equals to the logarithm of the average 
 * length of a record in the file. 
 *
 * Each element is stored separately, the lower s bits and the remaining 
 * upper bits, where s = floor(log(u/n)). The lower bits are stored contiguously 
 * while the upper bits are stored in an array U of size (n + x_{n-1} >> s) 
 * bits. Suppose k = x_{i} >> s + i, then set U[k] = 1. For example, 
 * x0=5, x1=10, x2=15, x3=20. 
 * Thus, n = 4, u = 21, s = floor(log(21/4)) = 1.  
 *
 * The length of U is 4 + 20 >> s = 14. 
 * k0 = x0 >> s + 0 = 2, U[2] = 1. Similarly, U[6] = 1, U[9] = 1, and U[13] = 1. 
 * The lower bits are stored as [1, 0, 1, 0]. To recover the original higher 
 * bits, one needs to select the i-th bit in U and subtract i. To recover 
 * x1 in the previous example, we need to select 1st 1 in U and the position 
 * is 6. By subtracting 1, we get 5 (101b) for upper bits. Combining with 
 * lower bit 0, we get x1 = 1010b = 10.
 * 
 * @param[in] g the graph
 * @param[in] spill_var_len variable length for spill, 0: not; 1: yes
 * @return 0 on success
 */

int build_efcode(bvgraph *g, int spill_var_len)
{
    eflist_initial(&(g->ef), g->n);
    int rval = load_efcode_from_file(g);
    if (rval) {
        load_efcode_online(g);
    }
    // build index structure for the upper array
    elias_fano_list *ef = &(g->ef);
    simple_select *sel = &((g->ef).sel);
    rval = simple_select_build(ef, g->n, sel, spill_var_len);
    if (rval < 0) {
        printf("WARNING: pre-allcated memory too small for spill structure.\nUse variable length instead.\n");
        return eflist_spill_too_small;
    }
    return (0);
}

