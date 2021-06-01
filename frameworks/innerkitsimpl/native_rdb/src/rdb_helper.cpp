/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rdb_helper.h"

#include "logger.h"
#include "rdb_errno.h"
#include "rdb_store_impl.h"
#include "unistd.h"

namespace OHOS {
namespace NativeRdb {
std::shared_ptr<RdbStore> RdbHelper::GetRdbStore(
    const RdbStoreConfig &config, int version, RdbOpenCallback &openCallback, int &errCode)
{
    std::shared_ptr<RdbStore> rdbStore = RdbStoreImpl::Open(config, errCode);
    if (rdbStore == nullptr) {
        LOG_ERROR("RdbHelper GetRdbStore fail to open RdbStore");
        return nullptr;
    }

    errCode = ProcessOpenCallback(*rdbStore, config, version, openCallback);
    if (errCode != E_OK) {
        LOG_ERROR("RdbHelper GetRdbStore ProcessOpenCallback fail");
        return nullptr;
    }

    return rdbStore;
}

int RdbHelper::ProcessOpenCallback(
    RdbStore &rdbStore, const RdbStoreConfig &config, int version, RdbOpenCallback &openCallback)
{
    int currentVersion;
    int errCode = rdbStore.GetVersion(currentVersion);
    if (errCode != E_OK) {
        return errCode;
    }
    if (version == currentVersion) {
        return openCallback.OnOpen(rdbStore);
    }

    if (config.IsReadOnly()) {
        LOG_ERROR("RdbHelper ProcessOpenCallback Can't upgrade read-only store");
        return E_CANNT_UPDATE_READONLY;
    }

    errCode = rdbStore.BeginTransaction();
    if (errCode != E_OK) {
        return errCode;
    }

    if (currentVersion == 0) {
        errCode = openCallback.OnCreate(rdbStore);
    } else if (version > currentVersion) {
        errCode = openCallback.OnUpgrade(rdbStore, currentVersion, version);
    } else {
        errCode = openCallback.OnDowngrade(rdbStore, currentVersion, version);
    }

    if (errCode == E_OK) {
        errCode = rdbStore.SetVersion(version);
    }

    if (errCode != E_OK) {
        rdbStore.MarkAsCommit();
        rdbStore.EndTransaction();
        return errCode;
    }

    errCode = rdbStore.MarkAsCommit();
    if (errCode != E_OK) {
        rdbStore.EndTransaction();
        return errCode;
    }

    errCode = rdbStore.EndTransaction();
    if (errCode != E_OK) {
        return errCode;
    }

    return openCallback.OnOpen(rdbStore);
}

int RdbHelper::DeleteRdbStore(const std::string &dbFileName)
{
    if (dbFileName.empty()) {
        return E_EMPTY_FILE_NAME;
    }

    if (access(dbFileName.c_str(), F_OK) != 0) {
        return E_OK; // not not exist
    }

    int result = remove(dbFileName.c_str());
    if (result != 0) {
        LOG_ERROR("RdbHelper DeleteRdbStore failed to delete the db file err = %{public}d", errno);
        return E_REMOVE_FILE;
    }

    int errCode = E_OK;
    std::string shmFileName = dbFileName + "-shm";
    if (access(shmFileName.c_str(), F_OK) == 0) {
        result = remove(shmFileName.c_str());
        if (result < 0) {
            LOG_ERROR("RdbHelper DeleteRdbStore failed to delete the shm file err = %{public}d", errno);
            errCode = E_REMOVE_FILE;
        }
    }

    std::string walFileName = dbFileName + "-wal";
    if (access(walFileName.c_str(), F_OK) == 0) {
        result = remove(walFileName.c_str());
        if (result < 0) {
            LOG_ERROR("RdbHelper DeleteRdbStore failed to delete the wal file err = %{public}d", errno);
            errCode = E_REMOVE_FILE;
        }
    }

    std::string journalFileName = dbFileName + "-journal";
    if (access(journalFileName.c_str(), F_OK) == 0) {
        result = remove(journalFileName.c_str());
        if (result < 0) {
            LOG_ERROR("RdbHelper DeleteRdbStore failed to delete the journal file err = %{public}d", errno);
            errCode = E_REMOVE_FILE;
        }
    }

    return errCode;
}
} // namespace NativeRdb
} // namespace OHOS