#include <freeDiameter/extension.h>
#include <freeDiameter/libfdproto.h>
#include <libpq-fe.h>
#include <json-c/json.h>

#define DCCA_APPLICATION_ID 4

static struct dict_object *dict_avp_result_code = NULL;
static struct dict_object *dict_cmd_ccr = NULL;

static json_object *pgresult_to_json(PGresult *res)
{
    if (!res)
        return NULL;

    int nrows = PQntuples(res);
    int nfields = PQnfields(res);

    json_object *jarray = json_object_new_array();

    for (int i = 0; i < nrows; i++)
    {
        json_object *jobj = json_object_new_object();
        for (int j = 0; j < nfields; j++)
        {
            const char *col_name = PQfname(res, j);
            const char *value = PQgetvalue(res, i, j);

            if (PQgetisnull(res, i, j))
            {
                json_object_object_add(jobj, col_name, NULL);
            }
            else
            {
                json_object *jvalue = json_object_new_string(value);
                json_object_object_add(jobj, col_name, jvalue);
            }
        }

        json_object_array_add(jarray, jobj);
    }
    printf("Converted PGresult to JSON successfully\n");

    return jarray;
}

static json_object *run_query(PGconn *conn, const char *query)
{

    // Submit the query and retrieve the result
    PGresult *res = PQexec(conn, query);

    // Check the status of the query result
    ExecStatusType resStatus = PQresultStatus(res);

    // Convert the status to a string and print it
    printf("Query Status: %s\n", PQresStatus(resStatus));

    // Check if the query execution was successful
    if (resStatus != PGRES_TUPLES_OK)
    {
        // If not successful, print the error message and finish the connection
        printf("Error while executing the query: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return NULL;
    }

    // We have successfully executed the query
    printf("Query Executed Successfully\n");

    // Convert the result to JSON
    json_object *jarray = pgresult_to_json(res);
    if(res){
        PQclear(res);
    }
    return jarray;
}

static PGconn *db_connection()
{
    const char *host = getenv("HOST");
    const char *port = getenv("PORT");
    const char *dbUser = getenv("DB_USER");
    const char *dbName = getenv("DB_NAME");

    if (!host || !port || !dbUser || !dbName)
    {
        fprintf(stderr, "One or more required PostgreSQL environment variables are not set.\n");
    }

    // Connect to the database
    // conninfo is a string of keywords and values separated by spaces.
    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s user=%s dbname=%s",
             host, port, dbUser, dbName);

    // Create a connection
    PGconn *conn = PQconnectdb(conninfo);

    // Check if the connection is successful
    if (!conn || PQstatus(conn) != CONNECTION_OK)
    {
        // If not successful, print the error message and finish the connection
        fprintf(stderr, "Error while connecting to the database server: %s\n", conn ? PQerrorMessage(conn) : "Connection object is NULL");

        // Free the connection object if it exists
        if (conn)
        {
            PQfinish(conn);
        }

        // Exit the program
        return NULL;
    }

    return conn;
}

// Handler for CCR
static int ccr_handler(struct msg **msg, struct avp *avp, struct session *sess,
                       void *cbdata, enum disp_action *act)
{
    TRACE_DEBUG(INFO, "CCR received in Gy handler");
    PGconn *conn;
    PGresult *response;
    conn = db_connection();
    if (!conn)
    {
        TRACE_DEBUG(INFO, "Database connection failed");
        return -1; // Handle connection failure gracefully
    }
    TRACE_DEBUG(INFO, "Database connection established successfully");
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT * FROM account_details where subscriberid='%s';",
             "+91876543212"); // Replace with actual subscriber ID extraction logic
    response = run_query(conn, query);
    if (response)
    {
        printf("Query Result: %s\n", json_object_to_json_string(response));
        json_object_put(response); // Free the JSON object after use
    } else
    {
        printf("No results found or query execution failed.\n");
    }

    if (conn)
    {
        PQfinish(conn);
    }

    struct msg *ans = NULL;
    struct avp *avp_result = NULL;
    union avp_value val;
    uint32_t result_code = 2001;

    CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, msg, 0), {TRACE_DEBUG(INFO,"Failed to create CCA answer"); return ENOENT; });
    ans = *msg;

    CHECK_FCT_DO(fd_msg_avp_new(dict_avp_result_code, 0, &avp_result), {TRACE_DEBUG(INFO, "Failed to create Result-Code AVP"); fd_msg_free(avp_result);});
    val.i32 = result_code;

    CHECK_FCT(fd_msg_avp_setvalue(avp_result, &val));
    CHECK_FCT(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, avp_result));

    //no need to transfer seperately already handled by diameter
    //CHECK_FCT_DO(fd_msg_send(&ans, NULL, NULL), {TRACE_DEBUG(INFO, "Failed to send CCA")});
    TRACE_DEBUG(INFO, "Sent CCA with Result-Code %u", result_code);

    return 0;
}

// init
int fd_ext_init(void)
{
    TRACE_DEBUG(INFO, "Initializing Gy plugin");
    

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