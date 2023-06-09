/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "napi_system_storage.h"

#include <string>

#include "js_ability.h"
#include "js_logger.h"
#include "js_utils.h"
#include "preferences_errno.h"
#include "preferences_helper.h"

using namespace OHOS::AppDataMgrJsKit;
using namespace OHOS::NativePreferences;

namespace OHOS {
namespace SystemStorageJsKit {
struct AsyncContext {
    std::string key;
    std::string def;
    std::string val;
    napi_ref success;
    napi_ref fail;
    napi_ref complete;
    napi_deferred deferred = nullptr;
    int32_t output = E_ERROR;
    napi_async_work request;
};

static const unsigned int MAX_KEY_LENGTH = 32;

static const unsigned int MAX_VALUE_LENGTH = 128;

static const int32_t FAILCOUNT = 2;

static const int32_t SUCCOUNT = 1;

void ParseString(napi_env env, napi_value &object, const char *name, const bool enable, std::string &output)
{
    napi_value value = nullptr;
    if (napi_get_named_property(env, object, name, &value) == napi_ok) {
        std::string key = JSUtils::Convert2String(env, value);
        NAPI_ASSERT_RETURN_VOID(env, enable || !key.empty(), "StorageOptions is empty.");
        output = std::move(key);
    }
}

void ParseFunction(napi_env env, napi_value &object, const char *name, napi_ref &output)
{
    napi_value value = nullptr;
    if (napi_get_named_property(env, object, name, &value) == napi_ok) {
        napi_valuetype valueType = napi_null;
        NAPI_CALL_RETURN_VOID(env, napi_typeof(env, value, &valueType));
        NAPI_ASSERT_RETURN_VOID(env, valueType == napi_function, "Wrong argument, function expected.");
        NAPI_CALL_RETURN_VOID(env, napi_create_reference(env, value, 1, &output));
    }
}

const std::string GetMessageInfo(int errCode)
{
    std::string message;
    switch (errCode) {
        case E_KEY_EMPTY:
            message = "The key string is null or empty.";
            break;
        case E_KEY_EXCEED_LENGTH_LIMIT:
            message = "The key string length should shorter than 32.";
            break;
        case E_VALUE_EXCEED_LENGTH_LIMIT:
            message = "The value string length should shorter than 128.";
            break;
        case E_DEFAULT_EXCEED_LENGTH_LIMIT:
            message = "The default string length should shorter than 128.";
            break;
        default:
            message = "unknown err";
    }
    return message;
}

void Complete(napi_env env, napi_status status, void *data)
{
    AsyncContext *ctx = static_cast<AsyncContext *>(data);
    if (status != napi_ok) {
        napi_throw_type_error(env, nullptr, "Execute callback failed.");
        return;
    }
    size_t len = 0;
    if (ctx->output == E_OK && ctx->success != nullptr) {
        napi_value successCallBack = nullptr;
        NAPI_CALL_RETURN_VOID(env, napi_get_reference_value(env, ctx->success, &successCallBack));
        napi_value succRes[SUCCOUNT] = { 0 };
        len = ctx->val.size();
        NAPI_CALL_RETURN_VOID(env, napi_create_string_utf8(env, ctx->val.c_str(), len, &succRes[0]));
        napi_value succCallbackResult = nullptr;
        NAPI_CALL_RETURN_VOID(
            env, napi_call_function(env, nullptr, successCallBack, SUCCOUNT, succRes, &succCallbackResult));
    }

    if (ctx->output != E_OK && ctx->fail != nullptr) {
        napi_value failCallBack = nullptr;
        NAPI_CALL_RETURN_VOID(env, napi_get_reference_value(env, ctx->fail, &failCallBack));
        napi_value failRes[FAILCOUNT] = { 0 };
        std::string message = GetMessageInfo(ctx->output);
        len = message.size();
        NAPI_CALL_RETURN_VOID(env, napi_create_string_utf8(env, message.c_str(), len, &failRes[0]));
        NAPI_CALL_RETURN_VOID(env, napi_create_int32(env, ctx->output, &failRes[1]));
        napi_value failCallbackResult = nullptr;
        NAPI_CALL_RETURN_VOID(
            env, napi_call_function(env, nullptr, failCallBack, FAILCOUNT, failRes, &failCallbackResult));
    }

    if (ctx->complete != nullptr) {
        napi_value completeCallBack = nullptr;
        NAPI_CALL_RETURN_VOID(env, napi_get_reference_value(env, ctx->complete, &completeCallBack));
        napi_value completeCallbackResult = nullptr;
        NAPI_CALL_RETURN_VOID(
            env, napi_call_function(env, nullptr, completeCallBack, 0, nullptr, &completeCallbackResult));
    }
    napi_delete_reference(env, ctx->success);
    napi_delete_reference(env, ctx->fail);
    napi_delete_reference(env, ctx->complete);
    napi_delete_async_work(env, ctx->request);
    napi_value res = nullptr;
    napi_get_undefined(env, &res);
    napi_resolve_deferred(env, ctx->deferred, res);
    delete ctx;
}

std::string GetPrefName(napi_env env)
{
    auto ctx = JSAbility::GetContext(env, nullptr);
    return ctx->GetPreferencesDir() + "/default.xml";
}

napi_value Operate(napi_env env, napi_callback_info info, const char *resource, napi_async_execute_callback execute)
{
    size_t argc = 1;
    napi_value argv[1] = { 0 };
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
    NAPI_ASSERT(env, argc == 1, "Not enough arguments, expected 1.");
    napi_valuetype valueType = napi_null;
    NAPI_CALL(env, napi_typeof(env, argv[0], &valueType));
    NAPI_ASSERT(env, valueType == napi_object, "Wrong argument type, object expected.");

    AsyncContext *context = new AsyncContext();

    ParseString(env, argv[0], "key", true, context->key);
    ParseString(env, argv[0], "value", false, context->val);
    ParseString(env, argv[0], "default", false, context->def);

    ParseFunction(env, argv[0], "success", context->success);
    ParseFunction(env, argv[0], "fail", context->fail);
    ParseFunction(env, argv[0], "complete", context->complete);

    napi_value resourceName = nullptr;
    NAPI_CALL(env, napi_create_string_utf8(env, resource, NAPI_AUTO_LENGTH, &resourceName));

    napi_value ret = nullptr;
    napi_create_promise(env, &context->deferred, &ret);

    NAPI_CALL(env, napi_create_async_work(env, nullptr, resourceName, execute, Complete, context, &context->request));
    NAPI_CALL(env, napi_queue_async_work(env, context->request));
    return ret;
}

napi_value NapiGet(napi_env env, napi_callback_info info)
{
    return Operate(env, info, "get", [](napi_env env, void *data) {
        AsyncContext *context = static_cast<AsyncContext *>(data);
        if (context->key.size() > MAX_KEY_LENGTH) {
            context->output = E_KEY_EXCEED_LENGTH_LIMIT;
            return;
        }

        if (context->def.size() > MAX_VALUE_LENGTH) {
            context->output = E_DEFAULT_EXCEED_LENGTH_LIMIT;
            return;
        }

        std::string prefName = GetPrefName(env);
        std::shared_ptr<Preferences> pref = PreferencesHelper::GetPreferences(prefName, context->output);
        context->val = pref->GetString(context->key, context->def);
    });
}

napi_value NapiSet(napi_env env, napi_callback_info info)
{
    return Operate(env, info, "set", [](napi_env env, void *data) {
        AsyncContext *context = static_cast<AsyncContext *>(data);
        if (context->key.size() > MAX_KEY_LENGTH) {
            context->output = E_KEY_EXCEED_LENGTH_LIMIT;
            return;
        }
        if (context->val.size() > MAX_VALUE_LENGTH) {
            context->output = E_VALUE_EXCEED_LENGTH_LIMIT;
            return;
        }

        std::string prefName = GetPrefName(env);
        std::shared_ptr<Preferences> pref = PreferencesHelper::GetPreferences(prefName, context->output);
        if (context->output != E_OK) {
            return;
        }
        context->output = pref->PutString(context->key, context->val);
    });
}

napi_value NapiDelete(napi_env env, napi_callback_info info)
{
    return Operate(env, info, "delete", [](napi_env env, void *data) {
        AsyncContext *context = static_cast<AsyncContext *>(data);
        if (context->key.size() > MAX_KEY_LENGTH) {
            context->output = E_KEY_EXCEED_LENGTH_LIMIT;
            return;
        }

        std::string prefName = GetPrefName(env);
        auto pref = PreferencesHelper::GetPreferences(prefName, context->output);
        if (context->output != E_OK) {
            return;
        }
        context->output = pref->Delete(context->key);
    });
}

napi_value NapiClear(napi_env env, napi_callback_info info)
{
    return Operate(env, info, "clear", [](napi_env env, void *data) {
        AsyncContext *context = static_cast<AsyncContext *>(data);
        std::string prefName = GetPrefName(env);
        std::shared_ptr<Preferences> pref = PreferencesHelper::GetPreferences(prefName, context->output);
        if (context->output != E_OK) {
            return;
        }
        context->output = pref->Clear();
    });
}

napi_value InitSystemStorage(napi_env env, napi_value exports)
{
    napi_property_descriptor properties[] = {
        DECLARE_NAPI_FUNCTION("get", NapiGet),
        DECLARE_NAPI_FUNCTION("delete", NapiDelete),
        DECLARE_NAPI_FUNCTION("clear", NapiClear),
        DECLARE_NAPI_FUNCTION("set", NapiSet),
    };
    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(properties) / sizeof(*properties), properties));
    return exports;
}
} // namespace SystemStorageJsKit
} // namespace OHOS