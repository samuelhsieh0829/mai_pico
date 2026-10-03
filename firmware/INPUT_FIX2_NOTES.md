# 觸控修正版 inputfix2（20260905）

## 交付狀態

- 新韌體：`mai_pico-20260905-inputfix2.uf2`。`mai_pico.uf2` 是同一份最新編譯輸出；第一版具版本檔名的 UF2 保留。
- 已修正濾波設定載入，加入三層狀態／邊緣計數；原本的外框事件佇列、四顆感測器、映射、靈敏度預設、故障處理和 9-byte 協定保留。
- 10 項 C 主機測試、2 項 Python 腳本案例通過，RP2040 編譯成功。
- **遊戲設定尚未套用。** 對工作區外 D 槽的提升權限程序兩次啟動失敗：存取被拒（os error 5）。實際 mai2.ini 仍為 DummyTouchPanel=1。這不是已完成的遊戲修正。
- 未停止或重新啟動 Sinmai，未燒錄控制器，未完成實機 30 分鐘驗收。

## 先處理遊戲模式，再測新版韌體

在一般終端執行下面指令（需 Python 3.10 以上）：

```powershell
python "C:\Users\samuel\Desktop\code\C++\maimai\mai_pico\firmware\apply_inputfix2_game.py"
```

腳本預設處理 `D:\SDGA_1.65-A\Package\mai2.ini`，先建立不覆寫舊備份的 `.before-inputfix2-時間-識別碼.bak`，再只將 DummyTouchPanel 的 1 改成 0，並逐位元組核對。原始 BOM、換行、其他設定保留；已經是 0 時不再修改。它不停止遊戲。可加 `--verify-only` 只檢查；或直接在文字編輯器中備份該檔後，將 `[AM]` 下的 `DummyTouchPanel=1` 改為 `DummyTouchPanel=0`。

1. 正常退出遊戲，重新啟動，以目前控制器韌體先測固定按住／放開。外框仍用 IO4，其他遊戲設定不需改動。
2. 記錄連點是否改善，再燒錄具版本名稱的 inputfix2 UF2。重新連線後手離開面板至少 1 秒，等初始化完成。
3. 在設定頁的進階終端執行 `cfg info`，確認 `firmware=20260905-inputfix2`；執行 `cfg diag` 保存初始計數。
4. 比較單區固定按住、多區按住、放開、滑動與快速連打；設定頁開啟及關閉各測一次。連續遊玩至少 30 分鐘。
5. 異常時先移開手並保存 `cfg diag` 輸出，再重設觸控，避免重校正抹掉當時的 baseline／filtered／delta 證據。

驗收標準：有效按住期间遊戲維持按住，各層放開計數不應增加；手放開後正常解除；持續遊玩無可重現的假連點或卡住。實機結果需另行填入，不能以主機測試代替。

## 已確認的修正

### 遊戲端

本機 Sinmai 的 InputManager.UpdateTouchPanel 在 DummyTouchPanel 模式下，每幀執行觸控記錄後會呼叫 SetNewTouchPanel(index, 0, counter + 1)。實體面板也使用這份狀態及計數，因此可能出現持續按住被清成 0 或因計數相同而跳過更新。game_touch_state_test 是依已檢查的本機 IL 建立的模型，重現 Dummy 模式 1/0 交替，實體模式維持 1；不是在測試中執行或修改遊戲組件。

### 韌體設定

濾波位元配置為 FFI[7:6]、SFI[5:4]、保留位[3]、ESI[2:0]。舊驗證式把 0xC0 的高半位元組當成超範圍，會重設整組 sense；現在全部 128 種合法組合都能保留，含原版 0x10 與 ESI 4–7。

移除了 0x10 強制遷移至 0xC0。只有保留位 bit 3 被設為 1 時，才修復 filter 欄位；其他合法靈敏度、各區微調、去彈跳不變。對其他真正損壞欄位的既有驗證維持。

如果舊韌體已將被重設的值寫回 Flash，這版無法憑空找回舊值；需依自己留存的設定或備份恢復。不要為套用此版執行 factory reset。

## 三層診斷判讀

`cfg diag` 保留原本 INPUT、HEALTH、MPR、FRAME，並增加：

| 輸出 | 意義 |
|---|---|
| TOUCH_LAYER layer=raw | 每顆 MPR121 最近一次成功狀態讀取，映射成 34 區；valid_sensors 表示最近狀態讀取是否有效。失敗時保留舊值，不偽造放開。 |
| TOUCH_LAYER layer=firmware | 經故障隔離與映射後，真正供遊戲傳輸使用的狀態。 |
| TOUCH_LAYER layer=usb | 最近一個完整排入 TinyUSB 的觸控封包狀態；valid=0 表示 USB 已失效／尚無完整封包。排入 USB 不等於主機或遊戲已收到。 |
| TOUCH_EDGE zone=A1 ... | raw、firmware、usb 分別列出「按下累計:放開累計」。週期重送相同狀態不增加邊緣計數。 |
| TOUCH_FORCED sensor=0 ... | 每顆晶片因 stale、oor、overcurrent、stopped、reconfigure、recovery 被軟體釋放的已按住且已映射電極次數。反覆隔離已放開的電極不重複累計。 |

HEALTH 另含 raw（晶片 12-bit 狀態）、raw_valid、raw_us（最近成功取樣時間）。USB 層另含 packets、last_us、interface、suspend_requests、usb_resets、suspend_neutral、reset_neutral。usb_resets 計的是韌體 USB 生命週期重設呼叫次數，不是拔插次數。中斷的半包不列為已完成封包；純讀取診斷不變更邊緣計數。

三層歷史從本次韌體啟動開始累計，讀取 cfg diag 或舊 stat reset 不清除；32-bit 計數滿後回繞。應在同一次啟動且映射未改動下，比較操作前後的差值。raw 是各晶片最近有效樣本的組合，四顆輪詢並非同一時刻；列出的狀態不能當成同步電氣量測。FRAME 同樣是分次讀取的診斷值。

- raw 按下／放開都反覆增加：晶片狀態本身在切換，需對照原始讀值、OOR 與基準追蹤。
- raw 穩定，firmware 有放開：對照 TOUCH_FORCED，判斷失敗隔離、重設或設定更新。
- firmware 穩定，usb 有放開：對照暫停／USB 重設中立封包。
- 三層均穩定而遊戲仍連點：優先確認 DummyTouchPanel=0 已經隨遊戲重啟生效，以及主機接收流程。

原版與本地仍都使用晶片觸控狀態；本次沒有改回獨立 delta 判定、任意提高門檻，或加入長按超時。感測器健康但打久仍卡住時，尚需實機資料才能判定基準漂移、接地或雜訊。

## 測試重現

在 mai_pico 目錄執行：

```powershell
cmake -S firmware/tests -B firmware/tests/build-host -G "MinGW Makefiles" -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/mingw32-make.exe
cmake --build firmware/tests/build-host --parallel 4
ctest --test-dir firmware/tests/build-host --output-on-failure
python firmware/tests/game_ini_test.py
```

主機測試涵蓋全部 256 種 filter 位元值的載入與再次載入、4 ms 短暫放開、過電流／OOR／逾時釋放歸因、持續按住的重送、USB 短寫完成前不計數，以及遊戲 Dummy 計數衝突模型。Python 測試只處理暫存 fixture，驗證只改一個位元組、完整備份、重复執行及不明設定拒絕處理。