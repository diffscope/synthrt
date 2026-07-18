// HandleTable.cpp — global handle table instances for the vnext C ABI
//
// Each accessor returns a reference to a function-local static table, which
// avoids order-of-initialization issues across translation units and keeps
// the table lifetime bounded by the process lifetime.

#include "HandleTable.h"

namespace srt::c_api {

HandleTable<RuntimeData> &runtimeTable() {
    static HandleTable<RuntimeData> table;
    return table;
}

HandleTable<LanguageServiceData> &languageServiceTable() {
    static HandleTable<LanguageServiceData> table;
    return table;
}

HandleTable<SessionData> &sessionTable() {
    static HandleTable<SessionData> table;
    return table;
}

HandleTable<ModelData> &modelTable() {
    static HandleTable<ModelData> table;
    return table;
}

HandleTable<TaskData> &taskTable() {
    static HandleTable<TaskData> table;
    return table;
}

} // namespace srt::c_api
