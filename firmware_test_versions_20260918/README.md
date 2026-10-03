# Mai Pico 韌體版本測試包

建立日期：2026-09-18（Asia/Taipei）

四份韌體皆以 Release 模式、Pico SDK 2.3.0、Arm GNU Toolchain 12.2.1 編譯，目標板為 RP2040 `pico`。

## 韌體

1. `01_remote_origin-main_e21fa23.uf2`
   - 來源：更新後的 `origin/main`
   - Git commit：`e21fa233cdf7d3ac31fcde46d102c044666aa19e`
   - 判定方式：MPR121 touch-status
   - 大小：152576 bytes
   - SHA-256：`B1092D4AD568A2013F803C53EDC31972C44F7F282B8EB8ED8EF037E4E496E6D4`

2. `02_local-main_e21fa23.uf2`
   - 來源：本機 `main`
   - Git commit：`e21fa233cdf7d3ac31fcde46d102c044666aa19e`
   - 判定方式：MPR121 touch-status
   - 大小：152576 bytes
   - SHA-256：`4DFC548F22A42874540309BFC49CC2C386D4AA7CD00F4DF961AEF6E401DA2D09`

3. `03_shared-branch_direct-delta-rebuilt.uf2`
   - 來源：分享頁「改用讀數判斷觸控按下」的核心 diff，套用到目前工作版本後重建
   - 判定方式：直接計算 `baseline - filtered`，使用 Press／Release 雙門檻遲滯
   - 每次輪詢一顆 MPR121，並保留目前版本的 I2C、OOR、overcurrent、stale 與 blocked-channel 故障隔離
   - 這不是分享頁歷史 container 所產生 UF2 的逐位元複本；分享頁沒有可下載的產物或 Git ref
   - 大小：183808 bytes
   - SHA-256：`20FF5205AAC07B6DD38D8748632FA4340666AEF06F048A6B8C62A1291FD89013`

4. `04_current-working_mpr121-status.uf2`
   - 來源：目前 `touch-raw-reading` 工作目錄
   - 基底 commit：`e21fa233cdf7d3ac31fcde46d102c044666aa19e`
   - 包含 2026-09-18 編譯當下尚未 commit 的 firmware 修改與新增來源檔
   - 判定方式：MPR121 touch-status；delta 只供診斷顯示
   - 大小：181760 bytes
   - SHA-256：`41E268C14074AFF8E7B717F107777A205DDD45512C57F8F6D725FC24ADC7ED4E`

`origin/main` 與本機 `main` 的來源內容及 submodule commit 相同。兩次獨立建置的 UF2 雜湊不同，是建置產物的非重現性差異；picotool 顯示兩者程式範圍、SDK、板型與 Release metadata 均相同。實際功能測試時可視為同一版本，但這裡依要求保留兩份。

## 建議測試方式

1. 一次只刷一份 UF2，重新插拔控制器後，在相同環境、相同接地與線材下測試。
2. 若要比較「各版本的預設行為」，每次刷完後在 Command Line Port 輸入 `factory`，等儲存完成，再重新插拔。注意這會清除自訂設定。
3. 若要比較「觸控判定方式」，建議直接做 `04 -> 03 -> 04`，不要在中途執行 `factory`，並確認 Filter、Sensitivity、Press、Release 設定相同。
4. 若要比較官方版與目前修改，建議做 `01 -> 04 -> 01`。`02` 可作為 `01` 的重複對照。

分享頁確認了歷史上確實有 direct-delta branch；但它沒有留下目前本機可見的 Git ref、worktree 或可下載 UF2，因此第 3 份是依分享頁 diff 重建的可測試版本。
