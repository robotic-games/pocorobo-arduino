# pocorobo-arduino

ポコロボ（Pocorobo）を Arduino IDE で C++ からプログラミングするための、ボードパッケージとライブラリです。

- Arduino IDE の「ボード」に **Pocorobo Standard** と **Pocorobo Mini** が追加されます
- ライブラリ `Pocorobo` で、サーボ・DC モータ・LED・ブザー・エンコーダ・専用コントローラの入力を、スタジオのブロックと同じ単位で扱えます
- スケッチ例が付属します。ネット接続が要るのは最初のインストールだけです

## 対応機種

| ボード名 | 機種 |
|---|---|
| Pocorobo Standard | ポコロボ スタンダード版 |
| Pocorobo Mini | ポコロボ ミニ版 |

## インストール

1. [Arduino IDE](https://www.arduino.cc/en/software)（2.x）をインストールします
2. 「ファイル → 基本設定」の **追加のボードマネージャの URL** に次を登録します
   ```
   https://robotic-games.github.io/pocorobo-arduino/package_pocorobo_index.json
   ```
3. 「ツール → ボード → ボードマネージャ」で **Pocorobo** を検索してインストールします（ESP32 用のツール一式も一緒に入ります。数分かかります）
4. 「ツール → ボード → Pocorobo ESP32 Boards」から **Pocorobo Standard** または **Pocorobo Mini** を選びます。ほかの設定は変えなくて大丈夫です

## 最初のスケッチ

1. 「ファイル → スケッチ例 → Pocorobo → MotorAndServo」を開きます
2. USB Type-C でロボットをパソコンにつなぎます
3. **スタジオ用のプログラム（標準のプログラム）が入っている本体は、初回だけ BOOT を押しながら RST を押して**書き込みモードにします。標準のプログラムが動いている間は、USB がパソコンとのネットワーク接続として動いていて、書き込み口が見えないためです
4. 「ツール → ポート」で本体のポートを選び、［→］（書き込み）を押します。書き込みが終わると自動で再起動して動き出します

2 回目からはボタン操作なしで書き込めます。スケッチで `Serial.println()` を使うと「ツール → シリアルモニタ」（115200）に出力が見えます（MotorAndServo は使っていないので何も出ません。使っている例は EncoderRead と Pairing です）。

```cpp
#include <Pocorobo.h>

void setup() {
  Poco.begin();                    // 装置と専用コントローラの受信を初期化
}

void loop() {
  Poco.motor(0).run(50);           // DC モータ 0 を 50 % で
  Poco.servo(0).angle(90);         // サーボ 0 を 90 度に
  Poco.led.set(0, 100, 0);         // LED を緑に
  delay(1000);
  Poco.stop();                     // 全部止める
  delay(1000);
}
```

## API

`#include <Pocorobo.h>` の 1 行で使えます。`setup()` で `Poco.begin()` を呼んでから使ってください。

| 呼び出し | 意味 | 値と範囲 |
|---|---|---|
| `Poco.begin()` | 装置と専用コントローラの受信を初期化 | `setup()` で 1 回。失敗すると `false` |
| `Poco.servo(id).angle(deg)` | サーボを角度で動かす | 0〜180 度（90 が中央）。id 0〜3 |
| `Poco.servo(id).pulse(us)` | サーボをパルス幅で動かす | 500〜2500 µs |
| `Poco.motor(id).run(percent)` | DC モータを回す | −100〜100 %（負で逆転、0 で停止）。id 0〜3（Mini は 0〜1） |
| `Poco.encoder(id).count()` | エンコーダの累積カウント | 正転で増える。Standard のみ |
| `Poco.encoder(id).reset()` | カウントを 0 にする | Standard のみ |
| `Poco.led.set(r, g, b)` | 本体のフルカラー LED | 各 0〜100（すべて 0 で消灯） |
| `Poco.buzzer.tone(hz)` / `Poco.buzzer.off()` | ブザーを鳴らす・止める | 100〜10000 Hz。Standard のみ |
| `Poco.stop()` | 全装置を初期状態へ | モータ 0・サーボの PWM 停止・LED 消灯・ブザー停止・エンコーダのカウント 0 |
| `Poco.controller.axis(id)` | 専用コントローラの十字キー | 0 = 左右、1 = 上下。−100〜100（右と下が正） |
| `Poco.controller.button(id)` | 専用コントローラのボタン | `true` / `false`。番号は下の表 |
| `Poco.controller.isConnected()` | 入力が届いているか | 直近 0.5 秒以内に受信していれば `true` |
| `Poco.controller.startPairing()` | ペアリングを始める | 初回だけ。詳しくは次の節 |

- Mini には無い装置（ブザー・エンコーダ）の関数は、Mini を選んでいると存在しません。呼ぶとコンパイル時にエラーになります
- 待つ・時間を測るには Arduino 標準の `delay()` / `millis()` をそのまま使います
- `Poco.stop()` はエンコーダのカウントも 0 に戻します。値が要るときは先に読んでください

## 専用コントローラ

ペアリング（登録）は最初の 1 回だけです。以後は電源を入れるだけで自動でつながります。

1. スケッチ例 **Pairing** を書き込み、本体のボタンを 2 秒長押しします（LED が青く点滅します）
2. コントローラのペアリングボタンを 1 回押します
3. LED が緑になれば完了です。赤なら失敗なので、もう一度 1 からやり直してください

ペアリング情報は本体に保存されます。スタジオ側でペアリング済みの本体は、そのまま使える場合があります（つながらなければ上の手順でペアリングし直してください）。

| `button(id)` の番号 | ボタン |
|---|---|
| 0 / 1 / 2 / 3 | A / B / X / Y |
| 12 / 13 / 14 / 15 | 十字キー 上 / 下 / 左 / 右 |

`axis(0)` / `axis(1)` は十字キーの左右・上下を −100 / 0 / 100 で返します（右と下が正）。

```cpp
#include <Pocorobo.h>

void setup() { Poco.begin(); }

void loop() {
  int x = Poco.controller.axis(0);     // 左右（右が正）
  int y = -Poco.controller.axis(1);    // 上下（上に押すと正になるよう反転）
  Poco.motor(0).run(y + x);            // 左モータ（範囲外はライブラリが ±100 に丸める）
  Poco.motor(1).run(y - x);            // 右モータ
  delay(10);
}
```

## スケッチ例

| 例 | 内容 |
|---|---|
| MotorAndServo | モータ 0 を前後に回し、サーボ 0 を 0 → 90 → 180 度に動かす |
| LedAndBuzzer | LED を赤 → 緑 → 青、ブザーでドレミ |
| EncoderRead | モータを回しながらエンコーダの値をシリアルモニタに出す |
| ControllerDrive | 専用コントローラで操縦する（ペアリング済みが前提） |
| Pairing | 本体のボタン長押しでペアリングを始め、状態を LED で示す |

## 拡張コネクタ

ライブラリが使っていないピンは、ふつうの Arduino のピンとして使えます。信号は **3.3 V** までです。電圧の高いセンサやモジュールを直結しないでください。

| コネクタ | Arduino での名前 | 備考 |
|---|---|---|
| I2C コネクタ（両機種） | `SDA` / `SCL` | 3.3 V。プルアップ抵抗は基板にありません（つなぐモジュール側の抵抗を使うか、外付けしてください） |
| UART コネクタ（Standard のみ） | `TX` / `RX` | `Serial1.begin(115200, SERIAL_8N1, RX, TX)` のように使います |
| サーボ・エンコーダのコネクタ | `PIN_SERVO[n]`、`PIN_ENCODER_A[n]` / `PIN_ENCODER_B[n]` | 使っていないコネクタの信号ピンを `pinMode()` / `digitalRead()` / `analogRead()` で流用できます |

SPI の既定ピン（`SS` / `MOSI` / `MISO` / `SCK`）はコネクタに出ていません。SPI を使うときは `SPI.begin(sck, miso, mosi, ss)` でピンを指定してください。

## スタジオ（標準のプログラム）に戻す

書き込みツール（<https://poco.robotic-sports.com/>）で、機種に合った標準のプログラム `firmware-pocorobo-<機種>-vX.Y.Z.bin` を書き込みます。本体に保存していたブロックのプログラムと設定は消えるので、必要ならスタジオ側に保存しておいてください。

## 困ったとき

| 症状 | 対処 |
|---|---|
| ポートが出てこない | 標準のプログラムが動いている本体は、BOOT を押しながら RST を押してから書き込みます。ケーブルがデータ通信に対応しているかも確認してください |
| `ボードに Pocorobo Standard または Pocorobo Mini を選んでください` と出る | 「ツール → ボード」で Pocorobo のボードを選んでいません |
| `'class PocoDevices' has no member named 'buzzer'` などと出る | Mini に無い装置を呼んでいます |
| 専用コントローラがつながらない | `Pairing` の例でペアリングし直してください。スケッチで `WiFi` ライブラリを使うと専用コントローラの受信と干渉します |
| ロボットが動かない・動きっぱなしになる | `loop()` を抜けてもモータは止まりません。止めたいところで `Poco.stop()` を呼んでください |

PSRAM の設定は変えないでください（有効にするとモータのピンと衝突します）。ボードを選べば既定で無効です。

## 開発者向け

| パス | 内容 |
|---|---|
| `platform/boards.txt` | ボード定義（`tools/make-boards-txt.py` で公式の ESP32 用定義から生成） |
| `platform/variants/*/pins_arduino.h` | ピン定義。**ロボットのファームウェアのビルド設定から自動生成されるので、手で編集しない** |
| `platform/tools/partitions/pocorobo.csv` | フラッシュの配置 |
| `libraries/Pocorobo/` | ライブラリ本体とスケッチ例（ボードパッケージに同梱される） |
| `tools/build-package.sh` | 公式の ESP32 用 Arduino 対応を土台に、ボードパッケージの zip を作る |
| `tools/gen-index.py` | ボードマネージャ用の index JSON を作る |
| `tools/compile-examples.sh` | 全スケッチ例を Standard / Mini でコンパイルする（CI と同じ手順） |

土台は arduino-esp32 3.3.11 に固定しています。リリースは `v*` タグを push すると GitHub Actions がパッケージを作り、Release への添付と index の更新まで行います。

## ライセンス

MIT License（[LICENSE](LICENSE)）
