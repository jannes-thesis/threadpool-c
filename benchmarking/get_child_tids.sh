#!/bin/bash
command_name=$1
thread_name=$2

ps H -C $command_name -o 'tid comm' | grep $thread_name | awk '{print $1}' | tr '\n' ',' | sed 's/.$//'
