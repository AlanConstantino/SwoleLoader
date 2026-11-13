#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

namespace sphaira::ui {
namespace {

auto GetModule(Result rc) -> const char* {
    switch (R_MODULE(rc)) {
        case Module_Svc: return "Svc";
        case Module_Fs: return "Fs";
        case Module_Os: return "Os";
        case Module_Ncm: return "Ncm";
        case Module_Ns: return "Ns";
        case Module_Spl: return "Spl";
        case Module_Applet: return "Applet";
        case Module_Usb: return "Usb";
        case Module_Irsensor: return "Irsensor";
        case Module_Libnx: return "Libnx";
        case Module_Sphaira: return APP_NAME;
    }

    return nullptr;
}
auto GetCodeMessage(Result rc) -> const char* {
    switch (rc) {
        case SvcError_TimedOut: return "SvcError_TimedOut";
        case SvcError_Cancelled: return "SvcError_Cancelled";

        case FsError_PathNotFound: return "FsError_PathNotFound";
        case FsError_PathAlreadyExists: return "FsError_PathAlreadyExists";
        case FsError_TargetLocked: return "FsError_TargetLocked";
        case FsError_TooLongPath: return "FsError_TooLongPath";
        case FsError_InvalidCharacter: return "FsError_InvalidCharacter";
        case FsError_InvalidOffset: return "FsError_InvalidOffset";
        case FsError_InvalidSize: return "FsError_InvalidSize";

        case Result_TransferCancelled: return "AppError_TransferCancelled";
        case Result_StreamBadSeek: return "AppError_StreamBadSeek";
        case Result_FsTooManyEntries: return "AppError_FsTooManyEntries";
        case Result_FsNewPathTooLarge: return "AppError_FsNewPathTooLarge";
        case Result_FsInvalidType: return "AppError_FsInvalidType";
        case Result_FsEmpty: return "AppError_FsEmpty";
        case Result_FsAlreadyRoot: return "AppError_FsAlreadyRoot";
        case Result_FsNoCurrentPath: return "AppError_FsNoCurrentPath";
        case Result_FsBrokenCurrentPath: return "AppError_FsBrokenCurrentPath";
        case Result_FsIndexOutOfBounds: return "AppError_FsIndexOutOfBounds";
        case Result_FsFsNotActive: return "AppError_FsFsNotActive";
        case Result_FsNewPathEmpty: return "AppError_FsNewPathEmpty";
        case Result_FsLoadingCancelled: return "AppError_FsLoadingCancelled";
        case Result_FsBrokenRoot: return "AppError_FsBrokenRoot";
        case Result_FsUnknownStdioError: return "AppError_FsUnknownStdioError";
        case Result_FsStdioFailedToSeek: return "AppError_FsStdioFailedToSeek";
        case Result_FsStdioFailedToRead: return "AppError_FsStdioFailedToRead";
        case Result_FsStdioFailedToWrite: return "AppError_FsStdioFailedToWrite";
        case Result_FsStdioFailedToOpenFile: return "AppError_FsStdioFailedToOpenFile";
        case Result_FsStdioFailedToCreate: return "AppError_FsStdioFailedToCreate";
        case Result_FsStdioFailedToTruncate: return "AppError_FsStdioFailedToTruncate";
        case Result_FsStdioFailedToFlush: return "AppError_FsStdioFailedToFlush";
        case Result_FsStdioFailedToCreateDirectory: return "AppError_FsStdioFailedToCreateDirectory";
        case Result_FsStdioFailedToDeleteFile: return "AppError_FsStdioFailedToDeleteFile";
        case Result_FsStdioFailedToDeleteDirectory: return "AppError_FsStdioFailedToDeleteDirectory";
        case Result_FsStdioFailedToOpenDirectory: return "AppError_FsStdioFailedToOpenDirectory";
        case Result_FsStdioFailedToRename: return "AppError_FsStdioFailedToRename";
        case Result_FsStdioFailedToStat: return "AppError_FsStdioFailedToStat";
        case Result_FsReadOnly: return "AppError_FsReadOnly";
        case Result_FsNotActive: return "AppError_FsNotActive";
        case Result_FsFailedStdioStat: return "AppError_FsFailedStdioStat";
        case Result_FsFailedStdioOpendir: return "AppError_FsFailedStdioOpendir";
        case Result_NroBadMagic: return "AppError_NroBadMagic";
        case Result_NroBadSize: return "AppError_NroBadSize";
        case Result_AppFailedMusicDownload: return "AppError_AppFailedMusicDownload";
        case Result_CurlFailedEasyInit: return "AppError_CurlFailedEasyInit";
        case Result_DumpFailedNetworkUpload: return "AppError_DumpFailedNetworkUpload";
        case Result_UnzOpen2_64: return "AppError_UnzOpen2_64";
        case Result_UnzGetGlobalInfo64: return "AppError_UnzGetGlobalInfo64";
        case Result_UnzLocateFile: return "AppError_UnzLocateFile";
        case Result_UnzGoToFirstFile: return "AppError_UnzGoToFirstFile";
        case Result_UnzGoToNextFile: return "AppError_UnzGoToNextFile";
        case Result_UnzOpenCurrentFile: return "AppError_UnzOpenCurrentFile";
        case Result_UnzGetCurrentFileInfo64: return "AppError_UnzGetCurrentFileInfo64";
        case Result_UnzReadCurrentFile: return "AppError_UnzReadCurrentFile";
        case Result_ZipOpen2_64: return "AppError_ZipOpen2_64";
        case Result_ZipOpenNewFileInZip: return "AppError_ZipOpenNewFileInZip";
        case Result_ZipWriteInFileInZip: return "AppError_ZipWriteInFileInZip";
        case Result_MmzBadLocalHeaderSig: return "AppError_MmzBadLocalHeaderSig";
        case Result_MmzBadLocalHeaderRead: return "AppError_MmzBadLocalHeaderRead";
        case Result_FileBrowserFailedUpload: return "AppError_FileBrowserFailedUpload";
        case Result_FileBrowserDirNotDaybreak: return "AppError_FileBrowserDirNotDaybreak";
        case Result_AppstoreFailedZipDownload: return "AppError_AppstoreFailedZipDownload";
        case Result_AppstoreFailedMd5: return "AppError_AppstoreFailedMd5";
        case Result_AppstoreFailedParseManifest: return "AppError_AppstoreFailedParseManifest";
        case Result_GameBadReadForDump: return "AppError_GameBadReadForDump";
        case Result_GameEmptyMetaEntries: return "AppError_GameEmptyMetaEntries";
        case Result_GameMultipleKeysFound: return "AppError_GameMultipleKeysFound";
        case Result_GameNoNspEntriesBuilt: return "AppError_GameNoNspEntriesBuilt";
        case Result_KeyMissingNcaKeyArea: return "AppError_KeyMissingNcaKeyArea";
        case Result_KeyMissingTitleKek: return "AppError_KeyMissingTitleKek";
        case Result_KeyMissingMasterKey: return "AppError_KeyMissingMasterKey";
        case Result_KeyFailedDecyptETicketDeviceKey: return "AppError_KeyFailedDecyptETicketDeviceKey";
        case Result_NcaFailedNcaHeaderHashVerify: return "AppError_NcaFailedNcaHeaderHashVerify";
        case Result_NcaBadSigKeyGen: return "AppError_NcaBadSigKeyGen";
        case Result_GcBadReadForDump: return "AppError_GcBadReadForDump";
        case Result_GcEmptyGamecard: return "AppError_GcEmptyGamecard";
        case Result_GcBadXciMagic: return "AppError_GcBadXciMagic";
        case Result_GcBadXciRomSize: return "AppError_GcBadXciRomSize";
        case Result_GcFailedToGetSecurityInfo: return "AppError_GcFailedToGetSecurityInfo";
        case Result_GhdlEmptyAsset: return "AppError_GhdlEmptyAsset";
        case Result_GhdlFailedToDownloadAsset: return "AppError_GhdlFailedToDownloadAsset";
        case Result_GhdlFailedToDownloadAssetJson: return "AppError_GhdlFailedToDownloadAssetJson";
        case Result_GhdlUnsafeArchivePath: return "AppError_GhdlUnsafeArchivePath";
        case Result_ThemezerFailedToDownloadThemeMeta: return "AppError_ThemezerFailedToDownloadThemeMeta";
        case Result_ThemezerFailedToDownloadTheme: return "AppError_ThemezerFailedToDownloadTheme";
        case Result_MainFailedToDownloadUpdate: return "AppError_MainFailedToDownloadUpdate";
        case Result_MainDownloadedUpdateInvalid: return "AppError_MainDownloadedUpdateInvalid";
        case Result_MainFailedToInstallUpdate: return "AppError_MainFailedToInstallUpdate";
        case Result_NxVersionsFailedToDownload: return "AppError_NxVersionsFailedToDownload";
        case Result_NxVersionsInvalidDatabase: return "AppError_NxVersionsInvalidDatabase";
        case Result_UsbDsBadDeviceSpeed: return "AppError_UsbDsBadDeviceSpeed";
        case Result_NcaBadMagic: return "AppError_NcaBadMagic";
        case Result_NspBadMagic: return "AppError_NspBadMagic";
        case Result_XciBadMagic: return "AppError_XciBadMagic";
        case Result_XciSecurePartitionNotFound: return "AppError_XciSecurePartitionNotFound";
        case Result_EsBadTitleKeyType: return "AppError_EsBadTitleKeyType";
        case Result_EsPersonalisedTicketDeviceIdMissmatch: return "AppError_EsPersonalisedTicketDeviceIdMissmatch";
        case Result_EsFailedDecryptPersonalisedTicket: return "AppError_EsFailedDecryptPersonalisedTicket";
        case Result_EsBadDecryptedPersonalisedTicketSize: return "AppError_EsBadDecryptedPersonalisedTicketSize";
        case Result_EsInvalidTicketBadRightsId: return "AppError_EsInvalidTicketBadRightsId";
        case Result_EsInvalidTicketFromatVersion: return "AppError_EsInvalidTicketFromatVersion";
        case Result_EsInvalidTicketKeyType: return "AppError_EsInvalidTicketKeyType";
        case Result_EsInvalidTicketKeyRevision: return "AppError_EsInvalidTicketKeyRevision";
        case Result_OwoBadArgs: return "AppError_OwoBadArgs";
        case Result_UsbCancelled: return "AppError_UsbCancelled";
        case Result_UsbBadMagic: return "AppError_UsbBadMagic";
        case Result_UsbBadVersion: return "AppError_UsbBadVersion";
        case Result_UsbBadCount: return "AppError_UsbBadCount";
        case Result_UsbBadBufferAlign: return "AppError_UsbBadBufferAlign";
        case Result_UsbBadTransferSize: return "AppError_UsbBadTransferSize";
        case Result_UsbEmptyTransferSize: return "AppError_UsbEmptyTransferSize";
        case Result_UsbOverflowTransferSize: return "AppError_UsbOverflowTransferSize";
        case Result_UsbUploadBadMagic: return "AppError_UsbUploadBadMagic";
        case Result_UsbUploadExit: return "AppError_UsbUploadExit";
        case Result_UsbUploadBadCount: return "AppError_UsbUploadBadCount";
        case Result_UsbUploadBadTransferSize: return "AppError_UsbUploadBadTransferSize";
        case Result_UsbUploadBadTotalSize: return "AppError_UsbUploadBadTotalSize";
        case Result_UsbUploadBadCommand: return "AppError_UsbUploadBadCommand";
        case Result_YatiContainerNotFound: return "AppError_YatiContainerNotFound";
        case Result_YatiNcaNotFound: return "AppError_YatiNcaNotFound";
        case Result_YatiInvalidNcaReadSize: return "AppError_YatiInvalidNcaReadSize";
        case Result_YatiInvalidNcaSigKeyGen: return "AppError_YatiInvalidNcaSigKeyGen";
        case Result_YatiInvalidNcaMagic: return "AppError_YatiInvalidNcaMagic";
        case Result_YatiInvalidNcaSignature0: return "AppError_YatiInvalidNcaSignature0";
        case Result_YatiInvalidNcaSignature1: return "AppError_YatiInvalidNcaSignature1";
        case Result_YatiInvalidNcaSha256: return "AppError_YatiInvalidNcaSha256";
        case Result_YatiNczSectionNotFound: return "AppError_YatiNczSectionNotFound";
        case Result_YatiInvalidNczSectionCount: return "AppError_YatiInvalidNczSectionCount";
        case Result_YatiNczBlockNotFound: return "AppError_YatiNczBlockNotFound";
        case Result_YatiInvalidNczBlockVersion: return "AppError_YatiInvalidNczBlockVersion";
        case Result_YatiInvalidNczBlockType: return "AppError_YatiInvalidNczBlockType";
        case Result_YatiInvalidNczBlockTotal: return "AppError_YatiInvalidNczBlockTotal";
        case Result_YatiInvalidNczBlockSizeExponent: return "AppError_YatiInvalidNczBlockSizeExponent";
        case Result_YatiInvalidNczZstdError: return "AppError_YatiInvalidNczZstdError";
        case Result_YatiTicketNotFound: return "AppError_YatiTicketNotFound";
        case Result_YatiInvalidTicketBadRightsId: return "AppError_YatiInvalidTicketBadRightsId";
        case Result_YatiCertNotFound: return "AppError_YatiCertNotFound";
        case Result_YatiNcmDbCorruptHeader: return "AppError_YatiNcmDbCorruptHeader";
        case Result_YatiNcmDbCorruptInfos: return "AppError_YatiNcmDbCorruptInfos";

        case Result_NszFailedCreateCctx: return "AppError_NszFailedCreateCctx";
        case Result_NszFailedSetCompressionLevel: return "AppError_NszFailedSetCompressionLevel";
        case Result_NszFailedSetThreadCount: return "AppError_NszFailedSetThreadCount";
        case Result_NszFailedSetLongDistanceMode: return "AppError_NszFailedSetLongDistanceMode";
        case Result_NszFailedResetCctx: return "AppError_NszFailedResetCctx";
        case Result_NszFailedCompress2: return "AppError_NszFailedCompress2";
        case Result_NszFailedCompressStream2: return "AppError_NszFailedCompressStream2";
        case Result_NszTooManyBlocks: return "AppError_NszTooManyBlocks";
        case Result_NszMissingBlocks: return "AppError_NszMissingBlocks";
        case Result_NspInvalidHeader: return "AppError_NspInvalidHeader";
        case Result_MspMissingManifest: return "AppError_MspMissingManifest";
        case Result_MspInvalidManifest: return "AppError_MspInvalidManifest";
        case Result_MspInvalidTitleId: return "AppError_MspInvalidTitleId";
        case Result_MspInvalidVersion: return "AppError_MspInvalidVersion";
        case Result_MspInvalidPatchset: return "AppError_MspInvalidPatchset";
        case Result_MspInvalidEntry: return "AppError_MspInvalidEntry";
        case Result_MspDuplicateEntry: return "AppError_MspDuplicateEntry";
        case Result_MspNoPayload: return "AppError_MspNoPayload";
        case Result_YatiHttpReadFailed: return "AppError_YatiHttpReadFailed";
        case Result_UsbBadPacketCrc: return "AppError_UsbBadPacketCrc";
        case Result_UsbBadDataCrc: return "AppError_UsbBadDataCrc";
        case Result_UsbRemoteError: return "AppError_UsbRemoteError";
        case Result_SaveInvalidArchiveLayout: return "AppError_SaveInvalidArchiveLayout";
        case Result_GcNoInitialData: return "AppError_GcNoInitialData";
        case Result_MainUpdateSignatureInvalid: return "AppError_MainUpdateSignatureInvalid";
        case Result_BatteryLow: return "AppError_BatteryLow";
    }

    return "";
}

// short, actionable advice for errors whose cause is known.
// only add codes here where the cause is confirmed, a wrong hint is worse than none.
auto GetCodeHint(Result rc) -> std::string {
    switch (rc) {
        case FsError_UsableSpaceNotEnoughSdCard:
        case FsError_UsableSpaceNotEnoughMmcUser:
        case FsError_UsableSpaceNotEnoughMmcSystem:
            return "Not enough free space on the install location. Free up space or change the install location."_i18n;

        case Result_KeyMissingNcaKeyArea:
        case Result_KeyMissingTitleKek:
        case Result_KeyMissingMasterKey:
        case Result_KeyFailedDecyptETicketDeviceKey:
            return "Your keys are missing or outdated. Dump prod.keys again with the latest Lockpick_RCM."_i18n;

        case Result_YatiInvalidNczZstdError:
            return "Failed to decompress the NCZ. Try installing from the SD card or use the USB install script."_i18n;

        case Result_GcEmptyGamecard:
            return "No game card was found. Make sure it is inserted correctly."_i18n;

        case FsError_TargetLocked:
            return "The file or folder is in use. Close any running software or restart the console."_i18n;
    }

    return {};
}

} // namespace

ErrorBox::ErrorBox(const std::string& message) : m_message{message} {
    log_write("[ERROR] %s\n", m_message.c_str());

    m_pos.w = 770.f;
    m_pos.h = 430.f;
    m_pos.x = 255;
    m_pos.y = 145;

    SetAction(Button::A, Action{[this](){
        SetPop();
    }});

    App::PlaySoundEffect(SoundEffect::Error);
}

ErrorBox::ErrorBox(Result code, const std::string& message) : ErrorBox{message} {
    m_code = code;
    m_code_message = GetCodeMessage(code);
    m_hint = GetCodeHint(code);
    m_code_module = std::to_string(R_MODULE(code));
    if (auto str = GetModule(code)) {
        m_code_module += " (" + std::string(str) + ")";
    }
    log_write("[ERROR] Code: 0x%X Module: %s Description: %u\n", R_VALUE(code), m_code_module.c_str(), R_DESCRIPTION(code));
}

ErrorBox::ErrorBox(Result code, const std::string& message, const std::string& detail) : ErrorBox{code, message} {
    if (!detail.empty()) {
        m_hint += (m_hint.empty() ? "" : "\n") + detail;
    }
}

auto ErrorBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);
}

auto ErrorBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));

    const Vec4 box = { 455, 470, 365, 65 };
    const auto center_x = m_pos.x + m_pos.w/2;

    gfx::drawTextArgs(vg, center_x, 180, 63, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR), "\uE140");
    if (m_code.has_value()) {
        const auto code = m_code.value();
        if (m_code_message.empty()) {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Code: 0x%X Module: %s", R_VALUE(code), m_code_module.c_str());
        } else {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_code_message.c_str());
        }
    } else {
        gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "An error occurred"_i18n.c_str());
    }
    const bool has_hint = !m_hint.empty();
    gfx::drawTextArgs(vg, center_x, has_hint ? 305 : 325, 23, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_message.c_str());
    if (has_hint) {
        gfx::drawTextBox(vg, m_pos.x + 35, 340, 20, m_pos.w - 70, theme->GetColour(ThemeEntryID_TEXT_SELECTED), m_hint.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }
    if (APP_UPDATE_REPO[0]) {
        gfx::drawTextArgs(vg, center_x, has_hint ? 395 : 380, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "If this message appears repeatedly, please open an issue."_i18n.c_str());
        gfx::drawTextArgs(vg, center_x, has_hint ? 425 : 415, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://github.com/" APP_UPDATE_REPO "/issues");
    }
    gfx::drawRectOutline(vg, theme, 4.f, box);
    gfx::drawTextArgs(vg, center_x, box.y + box.h/2, 23, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "OK"_i18n.c_str());
}

} // namespace sphaira::ui
