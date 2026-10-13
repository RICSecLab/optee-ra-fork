
<!--
-------------------------------------------------------------------------------------------------------------------
-->
# 日本語解説　OP-TEE Attester

Attester は Relying Party (Verifier) に対して、リモート認証のリクエストを送るクライアントです。このクライアントはTEE(Trusted Execution Environment, 具体的にはArm TrustZone)を持ち、 OP-TEE を実行します。現在のAttestaerは QEMU の上で構築されます。

![](../OPTEE-RA.png)

## 実行方法

Attester はコンテナ上の QEMU の上で起動されます。以下の 5 つの手順に従い、コンテナの起動し、 Verifier "Varaison"との通信プログラムを実行してください。

### 1. コンテナの起動

以下のコマンドを実行すると、Docker コンテナ上に入ります。
```sh
./container/start.sh
```

### 2. normal world のターミナルを開く

手順 1. でコンテナを起動したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の normal world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh normal
```

### 3. secure world のターミナルを開く

手順 1. と 2. で使用したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の secure world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh secure
```

### 4. ユーザが追加した CA/TA のビルドと、QEMU の起動とログイン

手順 1. で起動したターミナルで以下コマンドを実行してください。追加したアプリケーションを再ビルドし、QEMUを起動します。PTA は OP-TEE 本体の `veraison_attestation` PTA で、本プロジェクトのパッチ群はコンテナイメージのビルド時に当てられています（[`container/Dockerfile`](container/Dockerfile) 参照）。必要なのはビルドフラグだけです。
```sh
make -C ${OPTEE_DIR}/build run CFG_VERAISON_ATTESTATION_PTA=y -j
```

QEMU が立ち上がったら、c を入力します。
```sh
(qemu) c
```

次に、normal world のターミナル（2. で起動したターミナル）上で、`test` ユーザでログインします。
```sh
buildroot login: test
```

### 5. プログラムの実行

プログラムは normal world のターミナル上で起動します。
開発した evidence 生成プログラムを実行する場合は、以下のコマンドで実行できます。
```sh
optee_remote_attestation
```

## フォーマット方法

```sh
clang-format -i -style=file remote_attestation/host/*.c remote_attestation/host/*.h remote_attestation/ta/*.c remote_attestation/ta/*.h remote_attestation/ta/include/*.h
```
