#include <freeDiameter/extension.h>
#include <freeDiameter/libfdproto.h>
#include <libpq-fe.h>
#include <json-c/json.h>

#define DCCA_APPLICATION_ID 4

static struct dict_object *dict_avp_result_code = NULL;
static struct dict_object *dict_cmd_ccr = NULL;
static struct dict_object *dict_avp_crt = NULL;
static struct dict_object *dict_avp_sub_id = NULL;
static struct dict_object *dict_avp_sub_id_type = NULL;
static struct dict_object *dict_avp_req_action = NULL;
static struct dict_object *dict_avp_sub_id_data = NULL;
static struct dict_object *dict_avp_cbr = NULL;

static int dictionary_lookup(void);
static char *extractSubscriptionId(struct msg *msg);
static int extractRequestType(struct msg *msg);
static int extractRequestAction(struct msg *msg);
static float get_float_from_json(json_object *parent, const char *key, float default_value);

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
    if (res)
    {
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

    union avp_value val;
    struct msg *cca = NULL;

    struct avp *avp_result = NULL;
    struct avp *avp_chk_balance_result = NULL;

    uint32_t result_code = 2001;
    uint32_t cc_request_type;
    uint32_t requested_action;
    char *subscription_id = NULL;
    float account_balance = 0.0f;
    float threshold_balance = 0.0f;
    char query[512];

    PGconn *conn;
    json_object *response;
    conn = db_connection();
    if (!conn)
    {
        TRACE_DEBUG(INFO, "Database connection failed");
        return -1; // Handle connection failure gracefully
    }
    TRACE_DEBUG(INFO, "Database connection established successfully");

    // Fetch subscription Id from CCR
    subscription_id = extractSubscriptionId(*msg);
    if (subscription_id == NULL)
    {
        TRACE_DEBUG(INFO, "Subscription Id is NULL");
        return ENOENT;
    }

    // Fetch CC-Request-Type from CCR
    cc_request_type = extractRequestType(*msg);
    if (cc_request_type == (uint32_t)-1)
    {
        TRACE_DEBUG(INFO, "Request Type is NULL");
        return ENOENT;
    }

    if (cc_request_type == 4) // Event-Based
    {
        // Get Requested-Action AVP
        requested_action = extractRequestAction(*msg); // TODO FIX THE AVP PARSING
        // requested_action = (uint32_t)2;
        if (requested_action == (uint32_t)-1)
        {
            TRACE_DEBUG(INFO, "Requested Action is invalid");
            return ENOENT;
        }
        switch (requested_action)
        {
        case 2: // CHECK_BALANCE
            // Fetch balance for the given subscriptionId and set result value 0=ENOUGH_CREDIT 1=NO_CREDIT
            snprintf(query, sizeof(query),
                     "SELECT * FROM account_details where subscriberid='%s';",
                     subscription_id); // Replace with actual subscriber ID extraction logic
            response = run_query(conn, query);
            if (response)
            {
                printf("Query Result: %s\n", json_object_to_json_string(response));
                json_object *subscriber_record = json_object_array_get_idx(response, 0);
                if (subscriber_record == NULL || !json_object_is_type(subscriber_record, json_type_object))
                {
                    TRACE_DEBUG(INFO, "Error: No subscriber record found.\n");
                }
                else
                {
                    account_balance = get_float_from_json(subscriber_record, "u_current_balance", 0.0f);
                    threshold_balance = get_float_from_json(subscriber_record, "u_thresholds", 0.0f);
                }
                json_object_put(response); // Free the JSON object after use
            }
            else
            {
                printf("No results found or query execution failed.\n");
            }

            if (conn)
            {
                PQfinish(conn);
            }
            int chk_balance_result = (account_balance > threshold_balance) ? 0 : 1;
            val.i32 = chk_balance_result;
            CHECK_FCT_DO(fd_msg_avp_new(dict_avp_cbr, 0, &avp_chk_balance_result), {TRACE_DEBUG(INFO, "Failed to create Check Balance Result AVP")});
            CHECK_FCT(fd_msg_avp_setvalue(avp_chk_balance_result, &val));

            // TBC - Create a new AVP Granted Service Unit to send actual balance

            break;

        default:
            break;
        }
    }

    CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, msg, 0),
                 {
                     TRACE_DEBUG(INFO, "Failed to create CCA answer");
                     return ENOENT;
                 });
    cca = *msg;
    if (avp_chk_balance_result)
    {
        CHECK_FCT(fd_msg_avp_add(cca, MSG_BRW_LAST_CHILD, avp_chk_balance_result));
    }

    CHECK_FCT_DO(fd_msg_avp_new(dict_avp_result_code, 0, &avp_result), {TRACE_DEBUG(INFO, "Failed to create Result-Code AVP")});
    val.i32 = result_code;
    CHECK_FCT(fd_msg_avp_setvalue(avp_result, &val));
    CHECK_FCT(fd_msg_avp_add(cca, MSG_BRW_LAST_CHILD, avp_result));

    //CHECK_FCT_DO(fd_msg_send(&cca, NULL, NULL), {TRACE_DEBUG(INFO, "Failed to send CCA")});

    return 0;
}

int fd_ext_init(void)
{
    TRACE_DEBUG(INFO, "Initializing Gy plugin");

    const char *gy_application_id = getenv("GY_APPLICATION_ID");
    // Find application (Gy)
    struct dict_object *app = gy_application_id ? gy_application_id : (struct dict_object *)16777238; // Default to Gy application ID if not set

    struct dict_application_data data = {DCCA_APPLICATION_ID, "DCCA"};

    TRACE_DEBUG(INFO, "Checking Application");

    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID,
                                &data, &app, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Application not found");
                     return ENOENT;
                 });

    // Load dictionary objects
    CHECK_FCT_DO(dictionary_lookup(), return ENOENT);
    TRACE_DEBUG(INFO, "Dictionary objects loaded successfully.\n");

    // Advertise application support
    CHECK_FCT_DO(fd_disp_app_support(app, NULL, 1, 0),
                 {
                     TRACE_DEBUG(INFO, "Failed to advertise application support");
                     return EINVAL;
                 });

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

// Function to look up for AVPs in the Dictionary
static int dictionary_lookup(void)
{
    // Result-Code AVP
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict,
                                DICT_AVP,
                                AVP_BY_NAME,
                                "Result-Code",
                                &dict_avp_result_code,
                                ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Result-Code AVP not found");
                     return ENOENT;
                 });

    // Subscription-Id
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME, "Subscription-Id", &dict_avp_sub_id, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Subscription Id AVP not found");
                     return ENOENT;
                 });

    // Subscription-Id-Type
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME, "Subscription-Id-Type", &dict_avp_sub_id_type, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Subscription Id Type AVP not found");
                     return ENOENT;
                 });

    // Subscription-Id-Data
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME, "Subscription-Id-Data", &dict_avp_sub_id_data, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Subscription Id Data AVP not found");
                     return ENOENT;
                 });

    // Requested-Action
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME, "Requested-Action", &dict_avp_req_action, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Requested Action AVP not found");
                     return ENOENT;
                 });

    // CC-Request-Type
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME, "CC-Request-Type", &dict_avp_crt, ENOENT),
                 {
                     TRACE_DEBUG(INFO, "CC Request Type AVP not found");
                     return ENOENT;
                 });

    // CCR command
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict,
                                DICT_COMMAND,
                                CMD_BY_NAME,
                                "Credit-Control-Request",
                                &dict_cmd_ccr,
                                ENOENT),
                 {
                     TRACE_DEBUG(INFO, "CCR command not found");
                     return ENOENT;
                 });

    // Check-Balance-Result
    CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict,
                                DICT_AVP,
                                AVP_BY_NAME,
                                "Check-Balance-Result",
                                &dict_avp_cbr,
                                ENOENT),
                 {
                     TRACE_DEBUG(INFO, "Check Balance Result AVP not found");
                     return ENOENT;
                 });
    return 0;
}

static char *extractSubscriptionId(struct msg *ccr)
{
    TRACE_DEBUG(INFO, "Extracting subscription id");
    struct avp *avp_sub_id = NULL;
    struct avp *avp_child = NULL;
    struct avp_hdr *ahdr;

    int sub_id_type = -1;
    char *sub_id_data = NULL;

    // Find Subscription-Id AVP (Grouped)
    CHECK_FCT_DO(fd_msg_search_avp(ccr, dict_avp_sub_id, &avp_sub_id),
                 {
                     TRACE_DEBUG(INFO, "Subscription Id not found in CCR");
                     return NULL;
                 });

    if (avp_sub_id)
    {
        // Browse inside the Subscription-Id Grouped AVP
        CHECK_FCT_DO(fd_msg_browse(avp_sub_id, MSG_BRW_FIRST_CHILD, &avp_child, NULL), return NULL);

        while (avp_child)
        {
            CHECK_FCT_DO(fd_msg_avp_hdr(avp_child, &ahdr), return NULL);

            if (ahdr->avp_code == 450) // Subscription-Id-Type
            {
                sub_id_type = ahdr->avp_value->i32;
                TRACE_DEBUG(INFO, "Subscription-Id-Type: %d", sub_id_type);
            }
            else if (ahdr->avp_code == 444) // Subscription-Id-Data
            {
                sub_id_data = (char *)ahdr->avp_value->os.data;
                TRACE_DEBUG(INFO, "Subscription-Id-Data: %s", sub_id_data);
            }

            // Move to next child
            CHECK_FCT_DO(fd_msg_browse(avp_child, MSG_BRW_NEXT, &avp_child, NULL), return NULL);
        }
    }
    return sub_id_data;
}

static int extractRequestType(struct msg *msg)
{
    struct avp *avp_crt_data;
    struct avp_hdr *ahdr;

    // Get CC-Request-Type from CCR
    CHECK_FCT_DO(fd_msg_search_avp(msg, dict_avp_crt, &avp_crt_data),
                 {
                     TRACE_DEBUG(INFO, "CC-Request-Type not found in CCR");
                     return -1;
                 });
    CHECK_FCT_DO(fd_msg_avp_hdr(avp_crt_data, &ahdr),
                 {
                     TRACE_DEBUG(INFO, "error parsing CC-Request-Type in CCR");
                     return -1;
                 });

    if (!ahdr || !ahdr->avp_value)
    {
        TRACE_DEBUG(INFO, "Invalid AVP header or value.");
        return -1;
    }

    return ahdr->avp_value->i32;
}

static int extractRequestAction(struct msg *msg)
{

    struct avp *avp_req_action = NULL;
    struct avp_hdr *ahdr;

    // Try to find Requested-Action AVP
    int ret = fd_msg_search_avp(msg, dict_avp_req_action, &avp_req_action);
    if (ret != 0 || avp_req_action == NULL)
    {
        TRACE_DEBUG(INFO, "Requested-Action not found in CCR (fd_msg_search_avp ret=%d)", ret);
        return -1;
    }

    TRACE_DEBUG(INFO, "Requested-Action AVP found");

    ret = fd_msg_avp_hdr(avp_req_action, &ahdr);
    if (ret != 0 || ahdr == NULL || ahdr->avp_value == NULL)
    {
        TRACE_DEBUG(INFO, "Error parsing Requested-Action AVP header or value (ret=%d)", ret);
        return -1;
    }

    int val = ahdr->avp_value->i32;
    TRACE_DEBUG(INFO, "Requested-Action value: %d", val);
    return val;
}

static float get_float_from_json(json_object *parent, const char *key, float default_value)
{
    json_object *value_obj = NULL;

    if (!json_object_object_get_ex(parent, key, &value_obj))
    {
        TRACE_DEBUG(INFO, "'%s' not found in JSON object.\n", key);
        return default_value;
    }

    const char *value_str = json_object_get_string(value_obj);
    if (value_str == NULL)
    {
        TRACE_DEBUG(INFO, "String conversion failed for key '%s'.\n", key);
        return default_value;
    }

    return strtof(value_str, NULL);
}