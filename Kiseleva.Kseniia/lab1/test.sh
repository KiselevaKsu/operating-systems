#!/bin/bash

TEST_DIR="/home/bi/test_folder"

# ТЕСТ 1: Глубина 2
echo -e "\n[TEST 1] Depth = 2"
echo "Creating structure:"
echo "test_folder/"
echo "|-- root_file.txt"
echo "|-- other/"
echo "|-- sub1/"
echo "|   |-- sub2/"
echo "|       |-- sub3/"
echo "|       |-- important.txt"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1/sub2/sub3
mkdir -p $TEST_DIR/other
touch $TEST_DIR/root_file.txt
touch $TEST_DIR/sub1/sub2/important.txt

echo -e "\nBefore daemon run:"
ls -R $TEST_DIR

echo -e "\ninterval 2\n$TEST_DIR 2" > test_config.txt
./lab1_daemon test_config.txt
sleep 3

echo -e "\nAfter daemon run:"
ls -R $TEST_DIR

# При глубине 2: sub1 (1 ур) остается. 
# sub2 (2 ур) удаляется целиком (вместе с sub3 и important.txt).
# root_file.txt остается (так как он файл в корне).
# other (1 ур) остается.
if [ -d "$TEST_DIR/sub1" ] && [ ! -d "$TEST_DIR/sub1/sub2" ] && [ -f "$TEST_DIR/root_file.txt" ] && [ -d "$TEST_DIR/other" ]; then
    echo -e ">> TEST 1: PASSED (sub2 deleted entirely, root_file and sub1 kept)\n"
else
    echo -e ">> TEST 1: FAILED\n"
fi
kill $(cat /tmp/lab1_daemon.pid) 2>/dev/null
sleep 1

# ТЕСТ 2: Глубина 1
echo -e "\n[TEST 2] Depth = 1"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1/sub2
mkdir -p $TEST_DIR/other1
touch $TEST_DIR/file_root.txt
echo -e "\nBefore daemon run:"
ls -R $TEST_DIR

echo -e "\ninterval 2\n$TEST_DIR 1" > test_config.txt
./lab1_daemon test_config.txt
sleep 3

echo -e "\nAfter daemon run:"
ls -R $TEST_DIR

# При глубине 1: удаляются все папки 1-го уровня (sub1, other1).
# Файл file_root.txt остается.
if [ ! -d "$TEST_DIR/sub1" ] && [ ! -d "$TEST_DIR/other1" ] && [ -f "$TEST_DIR/file_root.txt" ]; then
    echo -e ">> TEST 2: PASSED (sub1 and other1 deleted, root file kept)\n"
else
    echo -e ">> TEST 2: FAILED\n"
fi
kill $(cat /tmp/lab1_daemon.pid) 2>/dev/null
sleep 1

# ТЕСТ 3: Защита от повторного запуска
echo -e "\n[TEST 3] Pid-file protection"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR
echo -e "interval 2\n$TEST_DIR 1" > test_config.txt

echo "Starting first daemon..."
./lab1_daemon test_config.txt
sleep 1
PID1=$(cat /tmp/lab1_daemon.pid)
echo "First daemon PID: $PID1"

echo "Starting second daemon (should kill first)..."
./lab1_daemon test_config.txt
sleep 2
PID2=$(cat /tmp/lab1_daemon.pid)
echo "Second daemon PID: $PID2"

if [ "$PID1" != "$PID2" ] && [ -n "$PID2" ]; then
    echo -e ">> TEST 3: PASSED (old daemon killed, new PID: $PID2)\n"
else
    echo -e ">> TEST 3: FAILED (PID mismatch or empty)\n"
fi
kill $PID2 2>/dev/null
sleep 1

# ТЕСТ 4: Неверный интервал
echo -e "\n[TEST 4] Invalid interval protection"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1
echo -e "interval abc\n$TEST_DIR 1" > test_config.txt

timeout 4 ./lab1_daemon test_config.txt
sleep 1

PID_CHECK=$(cat /tmp/lab1_daemon.pid 2>/dev/null)
if [ -n "$PID_CHECK" ] && kill -0 $PID_CHECK 2>/dev/null; then
    echo -e ">> TEST 4: PASSED (daemon started safely with default interval)\n"
    kill $PID_CHECK 2>/dev/null
else
    echo -e ">> TEST 4: FAILED (daemon didn't start with invalid interval)\n"
fi

rm -rf $TEST_DIR
rm -f test_config.txt
