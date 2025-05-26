#include <freeDiameter/extension.h>
#include <freeDiameter/libfdproto.h>
#include <libpq-fe.h>

#define DCCA_APPLICATION_ID 4

static struct dict_object *dict_avp_result_code = NULL;
static struct dict_object *dict_cmd_ccr = NULL;

static int db_connection()
{
    printf("libpq tutorial\n");

    // Connect to the database
    // conninfo is a string of keywords and values separated by spaces.
    char *conninfo = "dbname=priyads user=priyads host=host.docker.internal port=5432";

    // Create a connection
    PGconn *conn = PQconnectdb(conninfo);

    // Check if the connection is successful
    if (PQstatus(conn) != CONNECTION_OK)
    {
        // If not successful, print the error message and finish the connection
        printf("Error while connecting to the database server: %s\n", PQerrorMessage(conn));

        // Finish the connection
        PQfinish(conn);

        // Exit the program
        exit(1);
    }

    // We have successfully established a connection to the database server
    printf("Connection Established\n");
    printf("Port: %s\n", PQport(conn));
    printf("Host: %s\n", PQhost(conn));
    printf("DBName: %s\n", PQdb(conn));

    // Close the connection and free the memory
    PQfinish(conn);

    return 0;
}

// Handler for CCR
static int ccr_handler(struct msg **msg, struct avp *avp, struct session *sess,
                       void *cbdata, enum disp_action *act)
{
    TRACE_DEBUG(INFO, "CCR received in Gy handler");

    struct msg *ans = NULL;
    struct avp *avp_result = NULL;
    union avp_value val;
    uint32_t result_code = 2001;

    CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, msg, 0), {TRACE_DEBUG(INFO,"Failed to create CCA answer"); return ENOENT; });
    ans = *msg;

    CHECK_FCT_DO(fd_msg_avp_new(dict_avp_result_code, 0, &avp_result), {TRACE_DEBUG(INFO, "Failed to create Result-Code AVP")});
    val.i32 = result_code;

    CHECK_FCT(fd_msg_avp_setvalue(avp_result, &val));
    CHECK_FCT(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, avp_result));

    CHECK_FCT_DO(fd_msg_send(&ans, NULL, NULL), {TRACE_DEBUG(INFO, "Failed to send CCA")});

    TRACE_DEBUG(INFO, "Sent CCA with Result-Code %u", result_code);

    return 0;
}

// init
int fd_ext_init(void)
{
    TRACE_DEBUG(INFO, "Initializing Gy plugin");
    db_connection();

    // Find application (Gy)
    struct dict_object *app = NULL;
    struct dict_application_data data = {DCCA_APPLICATION_ID, "DCCA"};
    TRACE_DEBUG(INFO, "Checking Application");
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID,
                                &data, &app, ENOENT),
                 { TRACE_DEBUG(INFO, "Application not found"); return ENOENT; });

    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict,
                                DICT_AVP,
                                AVP_BY_NAME,
                                "Result-Code",
                                &dict_avp_result_code,
                                ENOENT),
                 {TRACE_DEBUG(INFO,"AVP not found"); return ENOENT; });

    CHECK_FCT_DO(fd_disp_app_support(app, NULL, 1, 0),
                 { TRACE_DEBUG(INFO, "Failed to advertise application support"); return EINVAL; });

    // CCR command
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict,
                                DICT_COMMAND,
                                CMD_BY_NAME,
                                "Credit-Control-Request",
                                &dict_cmd_ccr,
                                ENOENT),
                 {TRACE_DEBUG(INFO,"CCR command not found"); return ENOENT; });

    // Register CCR handler
    struct disp_when when;
    memset(&when, 0, sizeof(when));
    when.command = dict_cmd_ccr;

    CHECK_FCT(fd_disp_register(ccr_handler, DISP_HOW_ANY, &when, NULL, NULL));

    return 0;
}

// unload
void fd_ext_fini(void)
{
    TRACE_DEBUG(INFO, "Unloading Gy plugin");
}