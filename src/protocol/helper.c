/***********************************************************************************************************************************
Protocol Helper
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

#include "common/crypto/common.h"
#include "common/debug.h"
#include "common/exec.h"
#include "common/memContext.h"
#include "config/config.h"
#include "config/exec.h"
#include "config/protocol.h"
#include "protocol/helper.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_EXTERN(PROTOCOL_SERVICE_LOCAL_STR,                           PROTOCOL_SERVICE_LOCAL);
STRING_EXTERN(PROTOCOL_SERVICE_REMOTE_STR,                          PROTOCOL_SERVICE_REMOTE);

STRING_STATIC(PROTOCOL_STORAGE_TYPE_PG_STR,                         "db");
STRING_STATIC(PROTOCOL_STORAGE_TYPE_REPO_STR,                       "backup");

/***********************************************************************************************************************************
Local variables
***********************************************************************************************************************************/
typedef struct ProtocolHelperClient
{
    Exec *exec;                                                     // Executed client
    ProtocolClient *client;                                         // Protocol client
} ProtocolHelperClient;

static struct
{
    MemContext *memContext;                                         // Mem context for protocol helper

    unsigned int clientRemoteSize;                                  // Remote clients
    ProtocolHelperClient *clientRemote;

    unsigned int clientLocalSize;                                   // Local clients
    ProtocolHelperClient *clientLocal;
} protocolHelper;

/***********************************************************************************************************************************
Init local mem context and data structure
***********************************************************************************************************************************/
static void
protocolHelperInit(void)
{
    // In the protocol helper has not been initialized
    if (protocolHelper.memContext == NULL)
    {
        // Create a mem context to store protocol objects
        MEM_CONTEXT_BEGIN(memContextTop())
        {
            protocolHelper.memContext = memContextNew("ProtocolHelper");
        }
        MEM_CONTEXT_END();
    }
}

/***********************************************************************************************************************************
Is the repository local?
***********************************************************************************************************************************/
bool
repoIsLocal(void)
{
    FUNCTION_TEST_VOID();
    FUNCTION_TEST_RETURN(!cfgOptionTest(cfgOptRepoHost));
}

/***********************************************************************************************************************************
Error if the repository is not local
***********************************************************************************************************************************/
void
repoIsLocalVerify(void)
{
    FUNCTION_TEST_VOID();

    if (!repoIsLocal())
        THROW_FMT(HostInvalidError, "%s command must be run on the repository host", cfgCommandName(cfgCommand()));

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Is pg local?
***********************************************************************************************************************************/
bool
pgIsLocal(unsigned int hostId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, hostId);
    FUNCTION_LOG_END();

    ASSERT(hostId > 0);

    FUNCTION_LOG_RETURN(BOOL, !cfgOptionTest(cfgOptPgHost + hostId - 1));
}

/***********************************************************************************************************************************
Get the command line required for local protocol execution
***********************************************************************************************************************************/
static StringList *
protocolLocalParam(ProtocolStorageType protocolStorageType, unsigned int hostId, unsigned int protocolId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostId);
        FUNCTION_LOG_PARAM(UINT, protocolId);
    FUNCTION_LOG_END();

    ASSERT(hostId > 0);

    StringList *result = NULL;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Option replacements
        KeyValue *optionReplace = kvNew();

        // Add the command option
        kvPut(optionReplace, VARSTR(CFGOPT_COMMAND_STR), VARSTRZ(cfgCommandName(cfgCommand())));

        // Add the process id -- used when more than one process will be called
        kvPut(optionReplace, VARSTR(CFGOPT_PROCESS_STR), VARUINT(protocolId));

        // Add the host id
        kvPut(optionReplace, VARSTR(CFGOPT_HOST_ID_STR), VARUINT(hostId));

        // Add the storage type
        kvPut(optionReplace, VARSTR(CFGOPT_TYPE_STR), VARSTR(protocolStorageTypeStr(protocolStorageType)));

        // Only enable file logging on the local when requested
        kvPut(
            optionReplace, VARSTR(CFGOPT_LOG_LEVEL_FILE_STR),
            cfgOptionBool(cfgOptLogSubprocess) ? cfgOption(cfgOptLogLevelFile) : VARSTRDEF("off"));

        // Always output errors on stderr for debugging purposes
        kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_STDERR_STR), VARSTRDEF("error"));

        result = strLstMove(cfgExecParam(cfgCmdLocal, optionReplace, true), MEM_CONTEXT_OLD());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(STRING_LIST, result);
}

/***********************************************************************************************************************************
Get the local protocol client
***********************************************************************************************************************************/
ProtocolClient *
protocolLocalGet(ProtocolStorageType protocolStorageType, unsigned int hostId, unsigned int protocolId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostId);
        FUNCTION_LOG_PARAM(UINT, protocolId);
    FUNCTION_LOG_END();

    ASSERT(hostId > 0);

    protocolHelperInit();

    // Allocate the client cache
    if (protocolHelper.clientLocalSize == 0)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            protocolHelper.clientLocalSize = cfgOptionUInt(cfgOptProcessMax) + 1;
            protocolHelper.clientLocal = (ProtocolHelperClient *)memNew(
                protocolHelper.clientLocalSize * sizeof(ProtocolHelperClient));
        }
        MEM_CONTEXT_END();
    }

    ASSERT(protocolId <= protocolHelper.clientLocalSize);

    // Create protocol object
    ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientLocal[protocolId - 1];

    if (protocolHelperClient->client == NULL)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            // Execute the protocol command
            protocolHelperClient->exec = execNew(
                cfgExe(), protocolLocalParam(protocolStorageType, hostId, protocolId),
                strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u process", protocolId),
                (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * 1000));
            execOpen(protocolHelperClient->exec);

            // Create protocol object
            protocolHelperClient->client = protocolClientNew(
                strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u protocol", protocolId),
                PROTOCOL_SERVICE_LOCAL_STR, execIoRead(protocolHelperClient->exec), execIoWrite(protocolHelperClient->exec));

            protocolClientMove(protocolHelperClient->client, execMemContext(protocolHelperClient->exec));
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_LOG_RETURN(PROTOCOL_CLIENT, protocolHelperClient->client);
}

/***********************************************************************************************************************************
Get the command line required for remote protocol execution
***********************************************************************************************************************************/
static StringList *
protocolRemoteParam(ProtocolStorageType protocolStorageType, unsigned int protocolId, unsigned int hostIdx)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, protocolId);
        FUNCTION_LOG_PARAM(UINT, hostIdx);
    FUNCTION_LOG_END();

    // Is this a repo remote?
    bool isRepo = protocolStorageType == protocolStorageTypeRepo;

    // Fixed parameters for ssh command
    StringList *result = strLstNew();
    strLstAddZ(result, "-o");
    strLstAddZ(result, "LogLevel=error");
    strLstAddZ(result, "-o");
    strLstAddZ(result, "Compression=no");
    strLstAddZ(result, "-o");
    strLstAddZ(result, "PasswordAuthentication=no");

    // Append port if specified
    ConfigOption optHostPort = isRepo ? cfgOptRepoHostPort : cfgOptPgHostPort + hostIdx;

    if (cfgOptionTest(optHostPort))
    {
        strLstAddZ(result, "-p");
        strLstAdd(result, strNewFmt("%u", cfgOptionUInt(optHostPort)));
    }

    // Append user/host
    strLstAdd(
        result,
        strNewFmt(
            "%s@%s", strPtr(cfgOptionStr(isRepo ? cfgOptRepoHostUser : cfgOptPgHostUser + hostIdx)),
            strPtr(cfgOptionStr(isRepo ? cfgOptRepoHost : cfgOptPgHost + hostIdx))));

    // Option replacements
    KeyValue *optionReplace = kvNew();

    // Replace config options with the host versions
    unsigned int optConfig = isRepo ? cfgOptRepoHostConfig : cfgOptPgHostConfig + hostIdx;

    kvPut(optionReplace, VARSTR(CFGOPT_CONFIG_STR), cfgOptionSource(optConfig) != cfgSourceDefault  ? cfgOption(optConfig) : NULL);

    unsigned int optConfigIncludePath = isRepo ? cfgOptRepoHostConfigIncludePath : cfgOptPgHostConfigIncludePath + hostIdx;

    kvPut(
        optionReplace, VARSTR(CFGOPT_CONFIG_INCLUDE_PATH_STR),
        cfgOptionSource(optConfigIncludePath) != cfgSourceDefault ? cfgOption(optConfigIncludePath) : NULL);

    unsigned int optConfigPath = isRepo ? cfgOptRepoHostConfigPath : cfgOptPgHostConfigPath + hostIdx;

    kvPut(
        optionReplace, VARSTR(CFGOPT_CONFIG_PATH_STR),
        cfgOptionSource(optConfigPath) != cfgSourceDefault ? cfgOption(optConfigPath) : NULL);

    // Copy pg options to index 0 since that's what the remote will be expecting
    if (hostIdx != 0)
    {
        kvPut(optionReplace, VARSTR(CFGOPT_PG1_PATH_STR), cfgOption(cfgOptPgPath + hostIdx));
        kvPut(
            optionReplace, VARSTR(CFGOPT_PG1_SOCKET_PATH_STR),
            cfgOptionSource(cfgOptPgSocketPath + hostIdx) != cfgSourceDefault ? cfgOption(cfgOptPgSocketPath + hostIdx) : NULL);
        kvPut(
            optionReplace, VARSTR(CFGOPT_PG1_PORT_STR),
            cfgOptionSource(cfgOptPgPort + hostIdx) != cfgSourceDefault ? cfgOption(cfgOptPgPort + hostIdx) : NULL);
    }

    // Remove pg options that are not needed on the remote.  This is to reduce clustter and make debugging options easier.
    for (unsigned int pgIdx = 1; pgIdx < cfgOptionIndexTotal(cfgOptPgPath); pgIdx++)
    {
        kvPut(optionReplace, VARSTRZ(cfgOptionName(cfgOptPgPath + pgIdx)), NULL);
        kvPut(optionReplace, VARSTRZ(cfgOptionName(cfgOptPgSocketPath + pgIdx)), NULL);
        kvPut(optionReplace, VARSTRZ(cfgOptionName(cfgOptPgPort + pgIdx)), NULL);
    }

    // Add the command option (or use the current command option if it is valid)
    if (!cfgOptionTest(cfgOptCommand))
        kvPut(optionReplace, VARSTR(CFGOPT_COMMAND_STR), VARSTRZ(cfgCommandName(cfgCommand())));

    // Add the process id (or use the current process id if it is valid)
    if (!cfgOptionTest(cfgOptProcess))
        kvPut(optionReplace, VARSTR(CFGOPT_PROCESS_STR), VARINT((int)protocolId));

    // Don't pass log-path or lock-path since these are host specific
    kvPut(optionReplace, VARSTR(CFGOPT_LOG_PATH_STR), NULL);
    kvPut(optionReplace, VARSTR(CFGOPT_LOCK_PATH_STR), NULL);

    // Only enable file logging on the remote when requested
    kvPut(
        optionReplace, VARSTR(CFGOPT_LOG_LEVEL_FILE_STR),
        cfgOptionBool(cfgOptLogSubprocess) ? cfgOption(cfgOptLogLevelFile) : VARSTRDEF("off"));

    // Always output errors on stderr for debugging purposes
    kvPut(optionReplace, VARSTR(CFGOPT_LOG_LEVEL_STDERR_STR), VARSTRDEF("error"));

    // Add the type
    kvPut(optionReplace, VARSTR(CFGOPT_TYPE_STR), VARSTR(protocolStorageTypeStr(protocolStorageType)));

    StringList *commandExec = cfgExecParam(cfgCmdRemote, optionReplace, false);
    strLstInsert(commandExec, 0, cfgOptionStr(isRepo ? cfgOptRepoHostCmd : cfgOptPgHostCmd + hostIdx));
    strLstAdd(result, strLstJoin(commandExec, " "));

    FUNCTION_LOG_RETURN(STRING_LIST, result);
}

/***********************************************************************************************************************************
Get the remote protocol client
***********************************************************************************************************************************/
ProtocolClient *
protocolRemoteGet(ProtocolStorageType protocolStorageType, unsigned int hostId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(ENUM, protocolStorageType);
        FUNCTION_LOG_PARAM(UINT, hostId);
    FUNCTION_LOG_END();

    ASSERT(hostId > 0);

    // Is this a repo remote?
    bool isRepo = protocolStorageType == protocolStorageTypeRepo;

    protocolHelperInit();

    // Allocate the client cache
    if (protocolHelper.clientRemoteSize == 0)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            // The number of remotes allowed is the greater of allowed repo or pg configs + 1 (0 is reserved for connections from
            // the main process).  Since these are static and only one will be true it presents a problem for coverage.  We think
            // that pg remotes will always be greater but we'll protect that assumption with an assertion.
            ASSERT(cfgDefOptionIndexTotal(cfgDefOptPgPath) >= cfgDefOptionIndexTotal(cfgDefOptRepoPath));

            protocolHelper.clientRemoteSize = cfgDefOptionIndexTotal(cfgDefOptPgPath) + 1;
            protocolHelper.clientRemote = (ProtocolHelperClient *)memNew(
                protocolHelper.clientRemoteSize * sizeof(ProtocolHelperClient));
        }
        MEM_CONTEXT_END();
    }

    // Determine protocol id for the remote.  If the process option is set then use that since we want the remote protocol id to
    // match the local protocol id. Otherwise set to 0 since the remote is being started from a main process and there should only
    // be one remote per host.
    unsigned int protocolId = 0;

    // Use hostId to determine where to cache to remote
    unsigned int protocolIdx = hostId - 1;

    if (cfgOptionTest(cfgOptProcess))
        protocolId = cfgOptionUInt(cfgOptProcess);

    CHECK(protocolIdx < protocolHelper.clientRemoteSize);

    // Create protocol object
    ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientRemote[protocolIdx];

    if (protocolHelperClient->client == NULL)
    {
        MEM_CONTEXT_BEGIN(protocolHelper.memContext)
        {
            unsigned int optHost = isRepo ? cfgOptRepoHost : cfgOptPgHost + hostId - 1;

            // Execute the protocol command
            protocolHelperClient->exec = execNew(
                cfgOptionStr(cfgOptCmdSsh), protocolRemoteParam(protocolStorageType, protocolId, hostId - 1),
                strNewFmt(PROTOCOL_SERVICE_REMOTE "-%u process on '%s'", protocolId, strPtr(cfgOptionStr(optHost))),
                (TimeMSec)(cfgOptionDbl(cfgOptProtocolTimeout) * 1000));
            execOpen(protocolHelperClient->exec);

            // Create protocol object
            protocolHelperClient->client = protocolClientNew(
                strNewFmt(PROTOCOL_SERVICE_REMOTE "-%u protocol on '%s'", protocolId, strPtr(cfgOptionStr(optHost))),
                PROTOCOL_SERVICE_REMOTE_STR, execIoRead(protocolHelperClient->exec), execIoWrite(protocolHelperClient->exec));

            // Get cipher options from the remote if none are locally configured
            if (isRepo && strEq(cfgOptionStr(cfgOptRepoCipherType), CIPHER_TYPE_NONE_STR))
            {
                // Options to query
                VariantList *param = varLstNew();
                varLstAdd(param, varNewStr(CFGOPT_REPO1_CIPHER_TYPE_STR));
                varLstAdd(param, varNewStr(CFGOPT_REPO1_CIPHER_PASS_STR));

                VariantList *optionList = configProtocolOption(protocolHelperClient->client, param);

                if (!strEq(varStr(varLstGet(optionList, 0)), CIPHER_TYPE_NONE_STR))
                {
                    cfgOptionSet(cfgOptRepoCipherType, cfgSourceConfig, varLstGet(optionList, 0));
                    cfgOptionSet(cfgOptRepoCipherPass, cfgSourceConfig, varLstGet(optionList, 1));
                }
            }

            protocolClientMove(protocolHelperClient->client, execMemContext(protocolHelperClient->exec));
        }
        MEM_CONTEXT_END();
    }

    FUNCTION_LOG_RETURN(PROTOCOL_CLIENT, protocolHelperClient->client);
}

/**********************************************************************************************************************************/
void
protocolRemoteFree(unsigned int hostId)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, hostId);
    FUNCTION_LOG_END();

    ASSERT(hostId > 0);

    if (protocolHelper.clientRemote != NULL)
    {
        ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientRemote[hostId - 1];

        if (protocolHelperClient->client != NULL)
        {
            protocolClientFree(protocolHelperClient->client);
            execFree(protocolHelperClient->exec);

            protocolHelperClient->client = NULL;
            protocolHelperClient->exec = NULL;
        }
    }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Send keepalives to all remotes
***********************************************************************************************************************************/
void
protocolKeepAlive(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    if (protocolHelper.memContext != NULL)
    {
        for (unsigned int clientIdx  = 0; clientIdx < protocolHelper.clientRemoteSize; clientIdx++)
        {
            if (protocolHelper.clientRemote[clientIdx].client != NULL)
                protocolClientNoOp(protocolHelper.clientRemote[clientIdx].client);
        }
    }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Getters
***********************************************************************************************************************************/
ProtocolStorageType
protocolStorageTypeEnum(const String *type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STRING, type);
    FUNCTION_TEST_END();

    ASSERT(type != NULL);

    if (strEq(type, PROTOCOL_STORAGE_TYPE_PG_STR))
        FUNCTION_TEST_RETURN(protocolStorageTypePg);
    else if (strEq(type, PROTOCOL_STORAGE_TYPE_REPO_STR))
        FUNCTION_TEST_RETURN(protocolStorageTypeRepo);

    THROW_FMT(AssertError, "invalid protocol storage type '%s'", strPtr(type));
}

const String *
protocolStorageTypeStr(ProtocolStorageType type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
    FUNCTION_TEST_END();

    switch (type)
    {
        case protocolStorageTypePg:
            FUNCTION_TEST_RETURN(PROTOCOL_STORAGE_TYPE_PG_STR);

        case protocolStorageTypeRepo:
            FUNCTION_TEST_RETURN(PROTOCOL_STORAGE_TYPE_REPO_STR);
    }

    THROW_FMT(AssertError, "invalid protocol storage type %u", type);
}

/***********************************************************************************************************************************
Free the protocol objects and shutdown processes
***********************************************************************************************************************************/
void
protocolFree(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    if (protocolHelper.memContext != NULL)
    {
        // Free remotes
        for (unsigned int clientIdx = 0; clientIdx < protocolHelper.clientRemoteSize; clientIdx++)
            protocolRemoteFree(clientIdx + 1);

        // Free locals
        for (unsigned int clientIdx  = 0; clientIdx < protocolHelper.clientLocalSize; clientIdx++)
        {
            ProtocolHelperClient *protocolHelperClient = &protocolHelper.clientLocal[clientIdx];

            if (protocolHelperClient->client != NULL)
            {
                protocolClientFree(protocolHelperClient->client);
                execFree(protocolHelperClient->exec);

                protocolHelperClient->client = NULL;
                protocolHelperClient->exec = NULL;
            }
        }
    }

    FUNCTION_LOG_RETURN_VOID();
}
