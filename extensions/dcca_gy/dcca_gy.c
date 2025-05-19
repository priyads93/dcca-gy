#include <freeDiameter/extension.h>
#include <freeDiameter/libfdproto.h>

#define DCCA_APPLICATION_ID 4

static struct dict_object *dict_avp_result_code = NULL;
static struct dict_object *dict_cmd_ccr = NULL;

// Handler for CCR
static int ccr_handler(struct msg **msg, struct avp *avp, struct session *sess,
                       void *cbdata, enum disp_action *act) {
    TRACE_DEBUG(INFO, "CCR received in Gy handler");

    struct msg *ans = NULL;
    struct avp *avp_result = NULL;
    union avp_value val;
    uint32_t result_code = 2001;

    CHECK_FCT(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, msg, 0));
    ans = *msg;

    CHECK_FCT(fd_msg_avp_new(dict_avp_result_code, 0, &avp_result));
    val.i32 = result_code;
    CHECK_FCT(fd_msg_avp_setvalue(avp_result, &val));
    CHECK_FCT(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, avp_result));

    CHECK_FCT(fd_msg_send(&ans, NULL, NULL));
    TRACE_DEBUG(INFO, "Sent CCA with Result-Code %u", result_code);

    return 0;
}

// init
int fd_ext_init(void) {
    TRACE_DEBUG(INFO, "Initializing Gy plugin");
    
    // Find application (Gy)
    struct dict_object *app = NULL;
    struct dict_application_data data = { DCCA_APPLICATION_ID, "DCCA" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID, &data, &app, ENOENT));

    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict,
                             DICT_AVP,
                             AVP_BY_NAME,
                             "Result-Code",
                             &dict_avp_result_code,
                             ENOENT));

    // CCR command
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict,
                             DICT_COMMAND,
                             CMD_BY_NAME,
                             "Credit-Control-Request",
                             &dict_cmd_ccr,
                             ENOENT));

    // Register CCR handler
    struct disp_when when;
    memset(&when, 0, sizeof(when));
    when.command = dict_cmd_ccr;

    CHECK_FCT(fd_disp_register(ccr_handler, DISP_HOW_ANY, &when, NULL, NULL));

    return 0;
}

// unload
void fd_ext_fini(void) {
    TRACE_DEBUG(INFO, "Unloading Gy plugin");
}
