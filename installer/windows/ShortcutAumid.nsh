; Sets PKEY_AppUserModel_ID on an existing shortcut - required for
; Windows.UI.Notifications.ToastNotificationManager to resolve this app's
; notification permission (see native/src/platform/windows/ToastPermission.cpp).
; Adapted from https://github.com/safing/nsis-shortcut-properties
; (Apache-2.0), trimmed to just the AppUserModelID property - this app
; doesn't implement a toast-activation CLSID
; (INotificationActivationCallback), so setting
; PKEY_AppUserModel_ToastActivatorCLSID the way the original macro does
; would be actively wrong: it would tell Windows a COM handler exists for
; toast clicks when none does.
;
; No labels/Goto - this gets !insertmacro'd multiple times (once per
; shortcut) in the same script, and NSIS macro labels aren't scoped per
; expansion, so a fixed label name would collide on the second call.
; Nested ${If} blocks instead, so each step only runs if every prior step
; succeeded.

!include LogicLib.nsh
!include Win\Propkey.nsh
!include Win\COM.nsh

!macro SetShortcutAppUserModelId ShortcutPath AppID
  System::Store S

  !insertmacro ComHlpr_CreateInProcInstance ${CLSID_ShellLink} ${IID_IShellLink} r1 ".r0"
  ${If} $0 == 0
    ${IUnknown::QueryInterface} $1 '("${IID_IPersistFile}",.r2)i.r0'
    ${If} $0 == 0
      ${IPersistFile::Load} $2 "('${ShortcutPath}', 2)i.r0" ; STGM_READWRITE = 2
      ${If} $0 == 0
        ${IUnknown::QueryInterface} $1 '("${IID_IPropertyStore}",.r3)i.r0'
        ${If} $0 == 0
          System::Call '*${SYSSTRUCT_PROPERTYKEY}(${PKEY_AppUserModel_ID})p.r4'
          StrLen $7 "${AppID}"
          IntOp $7 $7 + 1
          IntOp $7 $7 * 2
          System::Call "ole32::CoTaskMemAlloc(i $7)p.r6"
          ${If} $6 <> 0
            StrLen $7 "${AppID}"
            IntOp $7 $7 + 1
            System::Call '*$6(&w$7 "${AppID}")'
            System::Call '*${SYSSTRUCT_PROPVARIANT}(${VT_LPWSTR},,p r6)p.r5'

            ${IPropertyStore::SetValue} $3 '($4,$5)i.r0'
            ${If} $0 == 0
              ${IPropertyStore::Commit} $3 "i.r0"
              ${If} $0 == 0
                ${IPersistFile::Save} $2 '("${ShortcutPath}",1)r.r0'
              ${EndIf}
            ${EndIf}

            System::Call "ole32::CoTaskMemFree(p r6)"
            System::Free $5
          ${Else}
            DetailPrint "SetShortcutAppUserModelId: CoTaskMemAlloc failed"
          ${EndIf}
          System::Free $4
          ${IUnknown::Release} $3 ""
        ${Else}
          DetailPrint "SetShortcutAppUserModelId: QueryInterface(IPropertyStore) failed ($0)"
        ${EndIf}
      ${Else}
        DetailPrint "SetShortcutAppUserModelId: Load failed ($0)"
      ${EndIf}
      ${IUnknown::Release} $2 ""
    ${Else}
      DetailPrint "SetShortcutAppUserModelId: QueryInterface(IPersistFile) failed ($0)"
    ${EndIf}
    ${IUnknown::Release} $1 ""
  ${Else}
    DetailPrint "SetShortcutAppUserModelId: CoCreateInstance failed ($0)"
  ${EndIf}

  System::Store L
!macroend
