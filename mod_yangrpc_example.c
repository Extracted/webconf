/* Include the required headers from httpd */
#define _GNU_SOURCE
#include "httpd.h"
#include "http_core.h"
#include "http_protocol.h"
#include "http_request.h"
#include "http_config.h"
#include "http_log.h"


#include <time.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <stdio.h>  /* required for file operations */
#include <unistd.h> /* required for changing the directory

/* YANG database access headers */
#include "ncx.h"
#include "val.h"
#include "val_util.h"
//#include "val_parse.h"
#include "yangrpc.h"

static char* server_address;
static int server_port;
static char* username;
static char* password;
static char* private_key_path;
static char* public_key_path;


/* Define prototypes of our functions in this module */
static void register_hooks(apr_pool_t *pool);
static int handler(request_rec *r);

typedef struct {
    yangrpc_cb_ptr_t yangrpc_cb_ptr;
} my_svr_cfg ;

static void* my_create_svr_conf(apr_pool_t* pool, server_rec* svr)
{
    my_svr_cfg* svr_cfg = (my_svr_cfg*)apr_pcalloc(pool, sizeof(my_svr_cfg));
    /* Set up the default values for fields of svr */

    svr_cfg->yangrpc_cb_ptr=NULL;
    return svr_cfg;
}

const char* server_address_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    server_address=strdup(arg);
    return NULL;
}

const char* server_port_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    server_port=atoi(arg);
    return NULL;
}

const char* username_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    username=strdup(arg);
    return NULL;
}

const char* password_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    password=strdup(arg);
    return NULL;
}

const char* private_key_path_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    private_key_path=strdup(arg);
    return NULL;
}

const char* public_key_path_cmd_func(cmd_parms* cmd, void* cfg, const char* arg)
{
    public_key_path=strdup(arg);
    return NULL;
}

static const command_rec my_cmds[] = {
    AP_INIT_TAKE1("ServerAddress", server_address_cmd_func, NULL/*my_ptr*/, OR_ALL, "Server address e.g. 127.0.0.1 or myserver.org"),
    AP_INIT_TAKE1("ServerPort", server_port_cmd_func, NULL/*my_ptr*/, OR_ALL, "Server port e.g. 830"),
    AP_INIT_TAKE1("Username", username_cmd_func, NULL/*my_ptr*/, OR_ALL, "Username e.g. root"),
    AP_INIT_TAKE1("Password", password_cmd_func, NULL/*my_ptr*/, OR_ALL, "Password e.g. mypass"),
    AP_INIT_TAKE1("PrivateKeyPath", private_key_path_cmd_func, NULL/*my_ptr*/, OR_ALL, "Private key path e.g. /root/.ssh/id_rsa"),
    AP_INIT_TAKE1("PublicKeyPath", public_key_path_cmd_func, NULL/*my_ptr*/, OR_ALL, "Public key path e.g. /root/.ssh/id_rsa.pub"),
    /* more directives as applicable */
    { NULL }
};

/* Define our module as an entity and assign a function for registering hooks  */

module AP_MODULE_DECLARE_DATA yangrpc_example_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,            // Per-directory configuration handler
    NULL,            // Merge handler for per-directory configurations
    my_create_svr_conf, // Per-server configuration handler
    NULL,            // Merge handler for per-server configurations
    my_cmds,            // Any directives we may have for httpd
    register_hooks   // Our hook registering function
};

/*
static char* get_username(int uid)
{
    struct passwd pwd;
    struct passwd *result;
    char* username;
    char *buf;
    size_t bufsize;
    int s;

    bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1)          // Value was indeterminate
        bufsize = 16384;        // Should be more than enough

    buf = malloc(bufsize);
    if (buf == NULL) {
        return NULL;
    }
    s = getpwuid_r(uid, &pwd, buf, bufsize, &result);
    if (result == NULL) {
        free(buf);
        return NULL;
    }
    username = malloc(strlen(pwd.pw_gecos)+1);
    strcpy(username, pwd.pw_name);
    free(buf);
    return username;
}*/

static int pre_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp)
{

    /* Change working directory. Otherwise the server wont find our files */
    int ret = chdir("/var/transpacket/webconf"); //Make sure directory exists if you get an error here
    if(ret != 0){
        printf("\nFailed to change working directory in pre_config(). Ret: %d", ret);
        return NULL;
    }

    server_port = 830;
    server_address = "127.0.0.1";
    username = "root";
    password = "hadm1_123";

    private_key_path = "/var/www/.ssh/id_rsa";
    public_key_path = "/var/www/.ssh/id_rsa.pub";

    return OK;
}
/* register_hooks: Adds a hook to the httpd process */
static void register_hooks(apr_pool_t *pool)
{
    ap_hook_pre_config(pre_config, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(handler, NULL, NULL, APR_HOOK_LAST);
}

static ssize_t writer_fn(void *cookie, const void *buffer, size_t size)
{
    size_t bytes_sent=0;
    while(bytes_sent<size) {
    	size_t res;
        res = ap_rwrite((char*)buffer+bytes_sent, size-bytes_sent, (request_rec *)cookie);
        if(res<=0) {
            return res;
        }
        bytes_sent+=res;
    }
}
boolean nodetest_fn (ncx_withdefaults_t withdef, boolean realtest, val_value_t *node)
{
    return val_is_config_data(node);
}

/*
 * Returns true if the string *str begins with the string *pre
 */
const bool startsWith(const char *str, const char *pre) {
    return strncmp(pre, str, strlen(pre)) == 0;
}

void serialize_val(request_rec *r, val_value_t* root_val)
{
    status_t res;
    FILE* fp;
    cookie_io_functions_t io_functions;
    xml_attrs_t        attrs;
    val_nodetest_fn_t  testfn;
    xml_init_attrs(&attrs);
    io_functions.read=NULL;
    io_functions.write=writer_fn;
    io_functions.seek=NULL;
    io_functions.close=NULL;


    fp=fopencookie (r, "w", io_functions);
    ap_set_content_type(r, "application/xml;charset=utf-8");


    /* If the url is the one that only wants config data, not state data */
    if(startsWith(r->uri, "/getconfiguration")) {
        /* Add the correct test that checks whether an element is config or state */
        testfn=nodetest_fn;
    } else {
        /* Add NULL as the test, which results in everything being outputted. This gives us the state data. */
        testfn=NULL;
    }

    res = xml_wr_check_open_file(fp,
                        root_val,
                        &attrs,
                        TRUE/*docmode*/,
                        FALSE/*xmlhdr*/,
                        TRUE/*withns*/,
                        0/*startindent*/,
                        4/*indent*/,
                        testfn);
    fclose(fp);

}

static int util_read(request_rec *r, const char **rbuf)
{
    int rc;

    if ((rc = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR)) != OK) {
        return rc;
    }

    if (ap_should_client_block(r)) {
        char buff[AP_IOBUFSIZE];
        int rsize, len_read, rpos=0;
        long length = r->remaining;
        *rbuf = apr_pcalloc(r->pool, length + 1);

        while ((len_read = ap_get_client_block(r, buff, sizeof(buff)))>0) {
            if ((rpos + len_read) > length) {
                rsize = length - rpos;
            }
            else {
                rsize = len_read;
            }
            memcpy((char*)*rbuf + rpos, buff, rsize);
            rpos += rsize;
        }
    }
    return rc;
}

#define DEFAULT_ENCTYPE "application/x-www-form-urlencoded"

static int read_post(request_rec *r, apr_table_t **tab)
{
    const char *data;
    const char *key, *val, *type;
    int rc = OK;

    if(r->method_number != M_POST) {
        return rc;
    }

    type = apr_table_get(r->headers_in, "Content-Type");
    if(strcasecmp(type, DEFAULT_ENCTYPE) != 0) {
        return DECLINED;
    }

    if((rc = util_read(r, &data)) != OK) {
        return rc;
    }

    *tab = apr_table_make(r->pool, 8);

    while(*data && (val = ap_getword(r->pool, &data, '&'))) {
        key = ap_getword(r->pool, &val, '=');

        ap_unescape_url((char*)key);
        {
            char *s;
            for(s = val; *s; ++s) {
                if ('+' == *s) {
                    *s = ' ';
                }
            }
        }

        ap_unescape_url((char*)val);

        apr_table_merge(*tab, key, val);
    }

    return OK;
}

/*
  init yangrpc and connect to server.
*/
static my_svr_cfg* rpc_connect(request_rec *r){
  my_svr_cfg* svr_cfg = ap_get_module_config(r->server->module_config, &yangrpc_example_module);

  if(svr_cfg->yangrpc_cb_ptr == NULL) {
    char* arg = "--keep-session-model-copies-after-compilation=false";
    int res = yangrpc_init(arg);
    assert(res==NO_ERR);

    printf("Connecting with address=%s, username=%s and password=%s\n", server_address, username, password);

    res = yangrpc_connect(server_address, server_port, username, password, public_key_path, private_key_path, NULL /*extra_args*/, &svr_cfg->yangrpc_cb_ptr);
    if(res!=NO_ERR) {
        printf("Failed to connect to %s with res: %d\n", server_address, res);
        assert(0);
        return NULL;
    }
  }

  printf("Connected to %s\n", server_address);
  return svr_cfg;
}

static int edit_config(request_rec *r)
{
    status_t res;
    ncx_module_t * ietf_netconf_mod;
    apr_table_t *tab=NULL;
    const char* config_xml_str;
    obj_template_t* root_obj;
    val_value_t* root_val;
    int rc;
    obj_template_t* edit_config_rpc_obj;
    obj_template_t* input_obj;
    obj_template_t* commit_rpc_obj;
    val_value_t* edit_config_rpc_val;
    val_value_t* edit_config_rpc_reply_val;
    val_value_t* commit_rpc_val;
    val_value_t* commit_rpc_reply_val;
    char* rpc_format_str;
    char* rpc_str;
    my_svr_cfg* svr_cfg;

    FILE *fp = NULL;
    rc = read_post(r, &tab);
    if(rc!=OK) return rc;

    config_xml_str = apr_table_get(tab, "config") ; //Get the POST data, which is the new configuration
    if(config_xml_str==NULL) {
        printf("POST data was null, declining request\n");
        return DECLINED;
    }

    svr_cfg = rpc_connect(r);
    if(svr_cfg == NULL){
      return DECLINED;
    }

    fp = fmemopen((void*)config_xml_str, strlen(config_xml_str), "r");
    res = ncxmod_load_module ("ietf-netconf", NULL, NULL, &ietf_netconf_mod);
    root_obj = ncx_find_object(ietf_netconf_mod, "config");

    rc = xml_rd_open_file (fp, root_obj, &root_val);


    edit_config_rpc_obj = ncx_find_object(ietf_netconf_mod, "edit-config");
    assert(obj_is_rpc(edit_config_rpc_obj));
    input_obj = obj_find_child(edit_config_rpc_obj, NULL, "input");
    assert(input_obj!=NULL);


    rpc_format_str = "<input><target><candidate/></target><default-operation>replace</default-operation>%s</input>";
    rpc_str = malloc(strlen(rpc_format_str)+strlen(config_xml_str)+1);
    sprintf(rpc_str, rpc_format_str, config_xml_str);

    edit_config_rpc_val = val_new_value();
    val_init_from_template(edit_config_rpc_val, edit_config_rpc_obj);
    res = val_set_cplxval_obj(edit_config_rpc_val, input_obj, rpc_str);
    free(rpc_str);
    if(res != NO_ERR) {
        printf("Res != NO_ERR after val_set_cplxval_obj() in edit_config()\n"); //Debug print

        val_free_value(edit_config_rpc_val);
        edit_config_rpc_val = NULL;
        edit_config_rpc_reply_val = NULL;

    } else {
        printf("edit_config(): Res == NO_ERR after val_set_cplxval_obj(), executing yangrpc command\n"); //Debug print

        res = yangrpc_exec(svr_cfg->yangrpc_cb_ptr, edit_config_rpc_val, &edit_config_rpc_reply_val);
        assert(res==NO_ERR);
    }

    if(edit_config_rpc_reply_val!=NULL && NULL!=val_find_child(edit_config_rpc_reply_val,NULL,"ok")) {
        printf("After yangrpc execution, ended up in branch 1\n"); //Debug print

        commit_rpc_obj = ncx_find_object(ietf_netconf_mod, "commit");
        assert(obj_is_rpc(commit_rpc_obj));
        commit_rpc_val = val_new_value();
        val_init_from_template(commit_rpc_val, commit_rpc_obj);
        res = yangrpc_exec(svr_cfg->yangrpc_cb_ptr, commit_rpc_val, &commit_rpc_reply_val);
        assert(res==NO_ERR);
    } else {
        printf("After yangrpc execution, ended up in branch 2\n"); //Debug print

        commit_rpc_val=NULL;
        commit_rpc_reply_val=NULL;
    }

    ap_rprintf(r,"<netconf-chat>");
    if(edit_config_rpc_val) {
        printf("------- edit_config_rpc_val is not null ------------\n"); //Debug print
        serialize_val(r, edit_config_rpc_val);
        val_free_value(edit_config_rpc_val);
    }
    if(edit_config_rpc_reply_val) {
        printf("------- edit_config_rpc_reply_val is not null ------------\n"); //Debug print
        serialize_val(r, edit_config_rpc_reply_val);
        val_free_value(edit_config_rpc_reply_val);
    }
    if(commit_rpc_val) {
        printf("------- commit_rpc_val is not null ------------\n"); //Debug print
        serialize_val(r, commit_rpc_val);
        val_free_value(commit_rpc_val);
    }
    if(commit_rpc_reply_val) {
        printf("------- commit_rpc_reply_val is not null ------------\n"); //Debug print
        serialize_val(r, commit_rpc_reply_val);
        val_free_value(commit_rpc_reply_val);
    }
    ap_rprintf(r,"</netconf-chat>");
    apr_table_clear(tab);
    return OK;
}

static int write_file(request_rec *r, char *filepath){
  char buffer[200];
  FILE* fr = fopen (filepath, "rt"); /* Open file using "read-text" mode */
  if(fr == NULL){
    printf(" Failed to open file\n");
    return DECLINED;
  }

  while(fgets(buffer, 200, fr) != NULL) { /* Read 200 bytes into buffer */
      ap_rputs (buffer, r); /* Write buffer */
  }

  fclose(fr);  /* close the file */

  printf(" Done\n");
  return OK;
}

static int load_configuration(request_rec *r){
    status_t res;
    ncx_module_t * ietf_netconf_mod;
    obj_template_t* rpc_obj;
    obj_template_t* input_obj;
    obj_template_t* filter_obj;

    val_value_t* request_val;
    val_value_t* reply_val;
    val_value_t* filter_val;
    val_value_t* type_meta_val;
    val_value_t* select_meta_val;
    my_svr_cfg* svr_cfg;

    svr_cfg = rpc_connect(r);
    if(svr_cfg == NULL){
      return DECLINED;
    }

    res = ncxmod_load_module ("ietf-netconf", NULL, NULL, &ietf_netconf_mod);
    assert(res==NO_ERR);

    rpc_obj = ncx_find_object(ietf_netconf_mod, "get");
    assert(obj_is_rpc(rpc_obj));
    input_obj = obj_find_child(rpc_obj, NULL, "input");
    assert(input_obj!=NULL);
    filter_obj = obj_find_child(input_obj, NULL, "filter");
    assert(filter_obj!=NULL);

    request_val = val_new_value();
    val_init_from_template(request_val, rpc_obj);
    filter_val = val_new_value();
    val_init_from_template(filter_val, filter_obj);

    type_meta_val = val_make_string(0, "type","xpath");
    select_meta_val = val_make_string(0, "select", "/");

    val_add_meta(select_meta_val, filter_val);
    val_add_meta(type_meta_val, filter_val);
    val_add_child(filter_val, request_val);

    res = yangrpc_exec(svr_cfg->yangrpc_cb_ptr, request_val, &reply_val);
    assert(res==NO_ERR);

    {
        obj_template_t* config_obj;
        val_value_t* config_val;
    	  val_value_t* data_val;
    	  val_value_t* interfaces_val;
    	  val_value_t* interface_val;
    	  char* interface_row_str[512];

        data_val = val_find_child(reply_val,NULL,"data");
        config_obj = ncx_find_object(ietf_netconf_mod, "config");
        config_val = val_new_value();
        val_init_from_template(config_val, config_obj);
        val_move_children(data_val,config_val);
        serialize_val(r, config_val);
        val_free_value(config_val);

    }
    val_free_value(request_val);
    val_free_value(reply_val);

    printf("Done serving current configuration\n");
    return OK;
}

/*
 * Handles setup requests
 */
static int handleSetup(char* args){
    char* pch, val;
    for(pch = strtok(args, "=&"); pch != NULL; pch = strtok(NULL, "=&")){
        char* val = strtok(NULL, "=&");
        if(strcmp(pch, "server") == 0){
            server_address = strdup(val);
        }if(strcmp(pch, "username") == 0){
            username = strdup(val);
        }if(strcmp(pch, "password") == 0){
            password = strdup(val);
        }
    }
    printf("Server ip set to %s\n", server_address);
    printf("Server username set to %s\n", username);
    printf("Server password set to %s\n", password);
    return 0;
}

/*
* The handler function for our module.
*/
static int handler(request_rec *r) {
    if(!r->uri){
        return DECLINED;
    }

    /* GET request to change server values */
    if( startsWith(r->uri, "/setup") ){
    	printf("Setup request received\n");
        handleSetup(r->args);
        return OK;
    }

    /* Configuration request */
    if( startsWith(r->uri, "/getconfiguration") ){
        printf("Config request received\n");
        load_configuration(r);
        return OK;
    }

    /* POST request with new configuration data */
    if( startsWith(r->uri, "/editconfig") ){
        printf("Edit request received\n");
        edit_config(r);
        return OK;
    }

    /* Default to file request */
    printf("Serving \"%s\"... ", r->uri +1); // +1 to remove "/" from the beginning
    return write_file(r, r->uri +1);
}
