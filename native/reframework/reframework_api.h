#pragma once

// ABI subset copied from REFramework's public include/reframework/API.h.
// Keep this file synchronized with the pinned REFramework baseline in README.md.
#include <stdbool.h>

#define REFRAMEWORK_PLUGIN_VERSION_MAJOR 1
#define REFRAMEWORK_PLUGIN_VERSION_MINOR 15
#define REFRAMEWORK_PLUGIN_VERSION_PATCH 0

typedef struct REFrameworkTypeDefinitionHandle__ { int unused; } *REFrameworkTypeDefinitionHandle;
typedef struct REFrameworkMethodHandle__ { int unused; } *REFrameworkMethodHandle;
typedef struct REFrameworkTDBHandle__ { int unused; } *REFrameworkTDBHandle;
typedef struct REFrameworkManagedObjectHandle__ { int unused; } *REFrameworkManagedObjectHandle;

typedef struct REFrameworkSDKData REFrameworkSDKData;

typedef void (*REFOnPresentCb)();
typedef bool (*REFOnPresentFn)(REFOnPresentCb);

typedef struct {
    int major;
    int minor;
    int patch;
    const char* game_name;
} REFrameworkPluginVersion;

typedef struct {
    void* reframework_module;
    const REFrameworkPluginVersion* version;
    const struct REFrameworkPluginFunctions* functions;
    const void* renderer_data;
    const REFrameworkSDKData* sdk;
} REFrameworkPluginInitializeParam;

typedef struct REFrameworkPluginFunctions {
    void* on_lua_state_created;
    void* on_lua_state_destroyed;
    REFOnPresentFn on_present;
    void* on_pre_application_entry;
    void* on_post_application_entry;
    void* lock_lua;
    void* unlock_lua;
    void* on_device_reset;
    void* on_message;
    void (*log_error)(const char* format, ...);
    void (*log_warn)(const char* format, ...);
    void (*log_info)(const char* format, ...);
    void* is_drawing_ui;
    void* create_script_state;
    void* delete_script_state;
    void* on_imgui_frame;
    void* on_imgui_draw_ui;
    void* on_pre_gui_draw_element;
} REFrameworkPluginFunctions;

typedef struct REFrameworkTDB REFrameworkTDB;
typedef struct REFrameworkTDBTypeDefinition REFrameworkTDBTypeDefinition;
typedef struct REFrameworkSDKFunctions REFrameworkSDKFunctions;

typedef int (*REFPreHookFn)(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr);
typedef void (*REFPostHookFn)(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr);

struct REFrameworkTDBTypeDefinition {
    unsigned int (*get_index)(REFrameworkTypeDefinitionHandle);
    unsigned int (*get_size)(REFrameworkTypeDefinitionHandle);
    unsigned int (*get_valuetype_size)(REFrameworkTypeDefinitionHandle);
    unsigned int (*get_fqn_hash)(REFrameworkTypeDefinitionHandle);
    const char* (*get_name)(REFrameworkTypeDefinitionHandle);
    const char* (*get_namespace)(REFrameworkTypeDefinitionHandle);
    void* get_full_name;
    void* has_fieldptr_offset;
    void* get_fieldptr_offset;
    unsigned int (*get_num_methods)(REFrameworkTypeDefinitionHandle);
    unsigned int (*get_num_fields)(REFrameworkTypeDefinitionHandle);
    unsigned int (*get_num_properties)(REFrameworkTypeDefinitionHandle);
    void* is_derived_from;
    void* is_derived_from_by_name;
    void* is_valuetype;
    void* is_enum;
    void* is_by_ref;
    void* is_pointer;
    void* is_primitive;
    void* get_vm_obj_type;
    REFrameworkMethodHandle (*find_method)(REFrameworkTypeDefinitionHandle, const char*);
};

struct REFrameworkTDB {
    void* get_num_types;
    void* get_num_methods;
    void* get_num_fields;
    void* get_num_properties;
    void* get_strings_size;
    void* get_raw_data_size;
    void* get_string_database;
    void* get_raw_database;
    void* get_type;
    REFrameworkTypeDefinitionHandle (*find_type)(REFrameworkTDBHandle, const char*);
};

typedef int REFrameworkResult;

struct REFrameworkTDBMethod {
    REFrameworkResult (*invoke)(REFrameworkMethodHandle, void*, void**, unsigned int, void*, unsigned int);
    void* (*get_function)(REFrameworkMethodHandle);
    const char* (*get_name)(REFrameworkMethodHandle);
    REFrameworkTypeDefinitionHandle (*get_declaring_type)(REFrameworkMethodHandle);
    REFrameworkTypeDefinitionHandle (*get_return_type)(REFrameworkMethodHandle);
    unsigned int (*get_num_params)(REFrameworkMethodHandle);
    void* get_params;
    unsigned int (*get_index)(REFrameworkMethodHandle);
    int (*get_virtual_index)(REFrameworkMethodHandle);
    bool (*is_static)(REFrameworkMethodHandle);
    unsigned short (*get_flags)(REFrameworkMethodHandle);
    unsigned short (*get_impl_flags)(REFrameworkMethodHandle);
    unsigned int (*get_invoke_id)(REFrameworkMethodHandle);
};

struct REFrameworkManagedObject {
    void (*add_ref)(REFrameworkManagedObjectHandle);
    void (*release)(REFrameworkManagedObjectHandle);
    REFrameworkTypeDefinitionHandle (*get_type_definition)(REFrameworkManagedObjectHandle);
    bool (*is_managed_object)(void*);
    unsigned int (*get_ref_count)(REFrameworkManagedObjectHandle);
    unsigned int (*get_size)(REFrameworkManagedObjectHandle);
    unsigned int (*get_vm_obj_type)(REFrameworkManagedObjectHandle);
    void* get_type_info;
    void* get_reflection_properties;
    void* get_reflection_property_descriptor;
    void* get_reflection_method_descriptor;
};

struct REFrameworkSDKFunctions {
    REFrameworkTDBHandle (*get_tdb)();
    void* get_resource_manager;
    void* get_vm_context;
    void* typeof_;
    REFrameworkManagedObjectHandle (*get_managed_singleton)(const char*);
    void* get_native_singleton;
    void* get_managed_singletons;
    void* get_native_singletons;
    void* create_managed_string;
    void* create_managed_string_normal;
    unsigned int (*add_hook)(REFrameworkMethodHandle, REFPreHookFn, REFPostHookFn, bool);
};

struct REFrameworkSDKData {
    const REFrameworkSDKFunctions* functions;
    const REFrameworkTDB* tdb;
    const REFrameworkTDBTypeDefinition* type_definition;
    const REFrameworkTDBMethod* method;
    const void* field;
    const void* property;
    const REFrameworkManagedObject* managed_object;
};

typedef struct {
    unsigned char bytes[128];
    bool exception_thrown;
} REFrameworkInvokeRet;

typedef bool (*REFPluginInitializeFn)(const REFrameworkPluginInitializeParam*);
