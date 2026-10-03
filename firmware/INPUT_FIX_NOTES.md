# 輸入修正版 20260905-inputfix1

## 交付與驗證狀態

- 韌體：`mai_pico-20260905-inputfix1.uf2` 是第一版留存檔；最新編譯輸出請參考 `INPUT_FIX2_NOTES.md`。
- 沿用工作區原有修改，保留 Flash 設定格式、GPIO／觸控映射、原有靈敏度及基準濾波設定。
- 9 項主機測試通過，RP2040 編譯成功。主機模擬不等於電容感測硬體或實際遊戲驗收。
- 尚未燒錄控制器，也尚未完成實機 30 分鐘遊玩驗收。

## 修正內容

- 外框按鍵由 1 kHz 定時器掃描，保留 3 ms 去彈跳。IO4／NKRO 各保存 64 筆變化，USB 送出成功才移除；溢位與重新連線先同步放開，再同步目前狀態。
- COIN 在掃描時累計，USB 等待不漏計；清除板卡狀態時不重播先前排隊的投幣。
- I²C 短暫失敗保留最後有效觸控，最長 20 ms；持續故障僅釋放該晶片的輸入。恢復通道要連續兩次有效讀到放開，才重新接受按下。
- 檢查 OOR、過電流及掃描停止。健康且仍按住的鄰近通道，不因另一電極 OOR 而整片重設；未映射電極的 OOR 不觸發整片重設。
- 感測器依步驟寫入及讀回設定，完成才啟動掃描。失敗恢復至少間隔 500 ms；I²C 空閒線路被拉低時，亦限頻嘗試釋放匯流排。
- 觸控在掃描後傳送，9-byte 封包要有完整空間才寫入。異常短寫先補完同一包，設定暫停期間也重試放開封包。
- 診斷延遲本身不算 I²C 故障；CLI 空白行／CRLF 不再把 NULL 交給 strlen。
- `cfg info` 正確標示 `mpr121_status`，並帶上版本識別。

MPR121 的設定寫入需在 Stop Mode，OOR／OVCF 解讀依據 [NXP MPR121 資料表第 5 節](https://www.nxp.com/docs/en/data-sheet/MPR121.pdf)。正常持續按住不另外加軟體超時重設；原有晶片基準追蹤行為保留。

## 燒錄與實機驗收

1. 按住 Pico 的 BOOTSEL 後接上 USB，將具版本名稱的 UF2 複製到 `RPI-RP2`。本版未變更設定儲存格式，不需 factory reset。
2. 重新連線或重設觸控後，手先離開面板至少 1 秒，讓分步設定與基準校正完成。
3. 用 Command Line Port 執行 `cfg diag`，確認 `firmware=20260905-inputfix1`、`timer=1`。MPR 的 ECR 應為 `8c`，沒有持續設定失敗；未接線電極的 OOR 要配合映射判讀。
4. 關閉設定頁，測試每顆外框快速連打、交替連打、同時多鍵及 COIN；觸控測試滑動、多點、長按後放開。再開即時監看重複操作。
5. 連續遊玩至少 30 分鐘。驗收標準為未出現可重現漏按、手離開後自動連點或永久按住；有效長按不被軟體中斷。
6. 若還有異常，先讓手完全離開，保留故障當下的 `cfg diag` 輸出，再決定是否長按 COIN 兩秒重設觸控。重設會改變基準，先取樣才有判讀依據。

## 診斷欄位

- `INPUT`：韌體版本、`timer`（定時器是否啟動）、`scan_max_us`（本次啟動最大按鍵掃描間隔）、各 HID 佇列溢位累計。正常運作不應持續增加溢位；Flash 寫入或關閉中斷可能拉長最大掃描間隔。
- `HEALTH`：各晶片的 I²C 讀取失敗、設定失敗、恢復嘗試、匯流排恢復累計，以及 `faults`、`blocked`、`oor`、`overcurrent`、`configuring`。`faults`／`blocked` 的 bit 0–11 對應實體電極。
- `MPR`：保留原本 ECR、OOR、AFE、filter、auto、CDC／CDT 等暫存器診斷。
- `FRAME`：48 個通道依 MPR0／ID0 到 MPR3／ID11 排列，每組為 `filtered:baseline:delta`。`valid=0` 表示至少一顆讀取失敗，其零值不能當成有效資料；不同暫存器的診斷值不是同步快照，不取代晶片觸控狀態。

如果手離開後 delta 仍高於放開門檻，而 I²C／設定／OOR 都正常，尚需依實機數據檢查基準漂移、接地、面板接線或雜訊；本版未用任意超時或提高門檻掩蓋這種狀況。

## 重現測試

Windows 主機測試（需要 CMake 與 MinGW GCC，在 mai_pico 目錄執行）：

```powershell
cmake -S firmware/tests -B firmware/tests/build-host -G "MinGW Makefiles" -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/mingw32-make.exe
cmake --build firmware/tests/build-host --parallel 4
ctest --test-dir firmware/tests/build-host --output-on-failure
```

RP2040 編譯沿用本機已設定好的 `firmware/build-pico` 與 Pico SDK，執行 `cmake --build firmware/build-pico --parallel 4`。Windows PATH 需包含現有工具鏈及 `C:/msys64/usr/bin`，因為建置腳本使用 touch／cp。
