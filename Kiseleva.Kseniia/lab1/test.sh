#!/bin/bash

TEST_DIR="/home/bi/test_folder"

# ТЕСТ 1: Глубина 2
echo -e "\n[TEST 1] Depth = 2"
echo "Creating structure:"
echo "test_folder/"
echo "|-- other/"
echo "|-- sub1/"
echo "|   |-- sub2/"
echo "|       |-- sub3/"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1/sub2/sub3
mkdir -p $TEST_DIR/other

echo -e "\nBefore daemon run:"
ls -R $TEST_DIR

echo -e "\ninterval 2\n$TEST_DIR 2" > test_config.txt
./lab1_daemon test_config.txt
sleep 3

echo -e "\nAfter daemon run:"
ls -R $TEST_DIR

# при глубине 2 удаляется sub1 (1 ур), other (1 ур), sub2 (2 ур).
# но sub3 (3 ур) должна быть оставлена
if [ -d "$TEST_DIR/sub1/sub2/sub3" ] && [ ! -d "$TEST_DIR/other" ]; then
    echo -e ">> TEST 1: PASSED (other and sub1/sub2 deleted, sub3 kept)\n"
else
    echo -e ">> TEST 1: FAILED\n"
fi
kill $(cat /tmp/lab1_daemon.pid) 2>/dev/null
sleep 1

# ТЕСТ 2: Глубина 1
echo -e "\n[TEST 2] Depth = 1"
echo "Creating structure:"
echo "test_folder/"
echo "|-- sub1/"
echo "|   |-- sub2/"
echo "|-- other1/"
echo "|-- other2/"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1/sub2
mkdir -p $TEST_DIR/other1
mkdir -p $TEST_DIR/other2

echo -e "\nBefore daemon run:"
ls -R $TEST_DIR

echo -e "\ninterval 2\n$TEST_DIR 1" > test_config.txt
./lab1_daemon test_config.txt
sleep 3

echo -e "\nAfter daemon run:"
ls -R $TEST_DIR

# при глубине 1 удаляются сами подпапки, но sub2 (2 ур) восстанавливается
if [ -d "$TEST_DIR/sub1/sub2" ] && [ ! -d "$TEST_DIR/other1" ] && [ ! -d "$TEST_DIR/other2" ]; then
    echo -e ">> TEST 2: PASSED (other1/2 deleted, sub2 kept)\n"
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
sleep 1
PID2=$(cat /tmp/lab1_daemon.pid)
echo "Second daemon PID: $PID2"

if [ "$PID1" != "$PID2" ] && [ -n "$PID2" ]; then
    echo -e ">> TEST 3: PASSED (old daemon killed, new PID: $PID2)\n"
else
    echo -e ">> TEST 3: FAILED (PID mismatch or empty)\n"
fi
kill $PID2 2>/dev/null
sleep 1

rm -rf $TEST_DIR
rm -f test_config.txt