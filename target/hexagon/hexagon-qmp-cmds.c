/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Hexagon CPU model discovery and expansion. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor.h"
#include "qobject/qdict.h"
#include "system/qmp-cpu.h"

CpuDefinitionInfoList *qmp_query_hexagon_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *head = NULL;
    GSList *classes = object_class_get_list(TYPE_HEXAGON_CPU, false);

    for (GSList *entry = classes; entry; entry = entry->next) {
        ObjectClass *oc = entry->data;
        const char *typename = object_class_get_name(oc);
        CpuDefinitionInfo *info = g_new0(CpuDefinitionInfo, 1);

        info->name = cpu_model_from_type(typename);
        info->q_typename = g_strdup(typename);
        /* Cross-version migration compatibility is not guaranteed. */
        info->q_static = false;
        QAPI_LIST_PREPEND(head, info);
    }
    g_slist_free(classes);
    return head;
}

static bool hexagon_cpu_apply_model_props(Object *obj, QObject *props,
                                          Error **errp)
{
    Visitor *v = qobject_input_visitor_new(props);
    const QDict *dict;
    bool success = false;

    if (!visit_start_struct(v, "model.props", NULL, 0, errp)) {
        goto out;
    }
    dict = qobject_to(QDict, props);
    if (qdict_haskey(dict, "ieee-fp") &&
        !object_property_set(obj, "ieee-fp", v, errp)) {
        goto end;
    }
    /* Reject unknown properties, including device links and tuning knobs. */
    success = visit_check_struct(v, errp);
end:
    visit_end_struct(v, NULL);
out:
    visit_free(v);
    return success;
}

CpuModelExpansionInfo *
qmp_query_hexagon_cpu_model_expansion(CpuModelExpansionType type,
                                     CpuModelInfo *model, Error **errp)
{
    g_autoptr(Object) obj = NULL;
    ObjectClass *oc;
    HexagonCPUClass *hcc;
    CpuModelExpansionInfo *info;
    QDict *props;
    bool ieee_fp;

    if (type != CPU_MODEL_EXPANSION_TYPE_FULL) {
        error_setg(errp, "The requested expansion type is not supported");
        return NULL;
    }
    oc = cpu_class_by_name(TYPE_HEXAGON_CPU, model->name);
    if (!oc) {
        error_setg(errp, "The CPU type '%s' is not a recognized Hexagon CPU "
                   "type", model->name);
        return NULL;
    }

    /* Query an unrealized CPU: no board resources or vCPU are needed. */
    obj = object_new(object_class_get_name(oc));
    if (model->props &&
        !hexagon_cpu_apply_model_props(obj, model->props, errp)) {
        return NULL;
    }

    hcc = HEXAGON_CPU_GET_CLASS(obj);
    ieee_fp = object_property_get_bool(obj, "ieee-fp", &error_abort) &&
              hcc->hex_def->hex_version >= HEX_VER_V68;
    props = qdict_new();
    qdict_put_bool(props, "ieee-fp", ieee_fp);

    info = g_new0(CpuModelExpansionInfo, 1);
    info->model = g_new0(CpuModelInfo, 1);
    info->model->name = g_strdup(model->name);
    info->model->props = QOBJECT(props);
    return info;
}
