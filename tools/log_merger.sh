#!/bin/bash
# ABOUT
# This script merges TSTAT log files in the specified directories.
# It is possible to configure the meging algorithms (by starting time, ending time or concatenation).
# It sorts all the log files in the specified directories, except for the current one ( the one with mod. time less than 60 min).
# After the merging, the old logs can be deleted (this configurable).
# Usage: log_merger.sh <out_dir> <in_dir1> <in_dir2> ... <in_dirN> 

# PARAMETERS
# 1. Process log files older than the guard time (in minutes)
time_guard_minutes=60
# 2. Delete merged directories
delete_merged=true
# 3. Debug level
debug=1
# 4. Log merging algorithm: can be <end> (sort by flow ending time), <start> (sort by flow starting time), <concat> (concatenate the files)
#    Please note: not for every log the three merging algorithms are available
tcp_complete="end"
tcp_nocomplete="end"
udp_complete="concat"
http_complete="start"
video_complete="end"
mm_complete="concat"
skype_complete="concat"
chat_complete="concat"
messages_complet="concat"

# CODE
# Parse arguments
if (( $# < 3 )) ; then
	echo "Usage: log_merger.sh <out_dir> <in_dir1> <in_dir2> ... <in_dirN> "
	exit
fi
# Output directory
output_dir=$(echo $1)
# Input directories to merge
input_dirs=""
for (( i=2; i<= $# ; i++ )); do
	input_dirs=$(echo $input_dirs ${!i})
done
if [ "$debug" -gt "0" ]; then
	echo "Output directory: $output_dir"
	echo "Input directories: $input_dirs"
fi

# Get the first directory and the other ones
first_input=$(echo $input_dirs | cut -d " " -f 1)
other_input=$(echo $input_dirs | cut -d " " -f 2-8)

# Iterate over all the directories
for dir1 in $(ls $first_input); do

	# Check it is a directory
	if [ ! -d "${first_input}/${dir1}" ] ; then
		continue
	fi

	# Check modify time
	dir_time=$(stat -c %X ${first_input}/${dir1})
	now=$(date +%s) 
	if (( $now - $dir_time <= $time_guard_minutes * 60 )) ; then
		if [ "$debug" -gt "0" ]; then
			minutes_ago=$(( ( $now - $dir_time ) / 60 ))
			echo "Skipping directory ${dir1}, modified $minutes_ago minutes ago."
		fi
		continue
	fi

	if [ "$debug" -gt "0" ]; then
		echo "Processing directory $dir1"
	fi
	found_other_dirs=true
	declare -A dir_to_process 
	dir_to_process[$first_input]="${first_input}/${dir1}"

	# Look in other input dirs if the directory exists
	for curr_in_dir in $other_input; do
	
		found_curr_dir=false

		# Search the exact name
		if [ -d "${curr_in_dir}/${dir1}" ]; then
			found_curr_dir=true
			dir_to_process[$curr_in_dir]="${curr_in_dir}/${dir1}"
		# Check if the are dictories with names refering to close minutes
		else
			dir1_date_human=$(echo $dir1 | cut -d "." -f1 | awk -F "_" '{print $1 "-" $2 "-" $3 " " $4 ":" $5 ":00"}')
			dir1_date_epoch=$(date -d "$dir1_date_human" +%s )
			#Try one minute before
			dir_less_epoch=$(( $dir1_date_epoch - 60 ))
			dir_less_tstat=$(date -d @$dir_less_epoch +%Y_%m_%d_%H_%M.out)
			if [ -d "${curr_in_dir}/$dir_less_tstat" ]; then
				if [ "$debug" -gt "0" ]; then
					echo "   In $curr_in_dir realligning on directory $dir_less_tstat."
				fi
				dir_to_process[$curr_in_dir]="${curr_in_dir}/$dir_less_tstat"
				found_curr_dir=true
			fi
			#Try one minute later
			dir_more_epoch=$(( $dir1_date_epoch + 60 ))
			dir_more_tstat=$(date -d @$dir_more_epoch +%Y_%m_%d_%H_%M.out)
			if [ -d "${curr_in_dir}/$dir_more_tstat" ]; then
				if [ "$debug" -gt "0" ]; then
					echo "   In $curr_in_dir realligning on directory $dir_more_tstat."
				fi
				dir_to_process[$curr_in_dir]="${curr_in_dir}/$dir_more_tstat"
				found_curr_dir=true
			fi
			
		fi

		# If no directory is found, unset the flag
		if ! $found_curr_dir ; then
			found_other_dirs=false
		fi
	done

	# Proceed only if all the directories have been found
	if $found_other_dirs; then
		if [ "$debug" -gt "0" ]; then
			echo "   Found!"
			# Print debug information
			for d in $input_dirs; do
				echo "      ${dir_to_process[$d]}"
			done
		fi
	else
		if [ "$debug" -gt "0" ]; then
			echo "Not found all files!"
		fi
		continue
	fi

	# Now in dir_to_process there are the directories of the log files to merge
	# Create outdir
	mkdir -p "${output_dir}/${dir1}"



	#1. Process log_tcp_complete
	if [ "$debug" -gt "0" ]; then
		echo "   Merging log_tcp_complete"
	fi
	files=""
	for indir in $input_dirs ; do
		files="${files} ${dir_to_process[$indir]}/log_tcp_complete"
	done
	# Print 1 header 
	head -1 "$first_input/$dir1/log_tcp_complete" > "${output_dir}/${dir1}/log_tcp_complete"
	# Concat the files
	if [ $tcp_complete = "start" ] ; then
		tail -q -n+2 $files | sort -n -k29 >> "${output_dir}/${dir1}/log_tcp_complete"
	fi
	if [ $tcp_complete = "end" ] ; then
		tail -q -n+2 $files | sort -n -k30 >> "${output_dir}/${dir1}/log_tcp_complete"
	fi
	if [ $tcp_complete = "concat" ] ; then
		tail -q -n+2 $files >> "${output_dir}/${dir1}/log_tcp_complete"
	fi



	#2. Process log_tcp_nocomplete
	if [ "$debug" -gt "0" ]; then
		echo "   Merging log_tcp_nocomplete"
	fi
	files=""
	for indir in $input_dirs ; do
		files="${files} ${dir_to_process[$indir]}/log_tcp_nocomplete"
	done
	# Print 1 header 
	head -1 "$first_input/$dir1/log_tcp_nocomplete" > "${output_dir}/${dir1}/log_tcp_nocomplete"
	# Concat the files
	if [ $tcp_nocomplete = "start" ] ; then
		tail -q -n+2 $files | sort -n -k29 >> "${output_dir}/${dir1}/log_tcp_nocomplete"
	fi
	if [ $tcp_nocomplete = "end" ] ; then
		tail -q -n+2 $files | sort -n -k30 >> "${output_dir}/${dir1}/log_tcp_nocomplete"
	fi
	if [ $tcp_nocomplete = "concat" ] ; then
		tail -q -n+2 $files >> "${output_dir}/${dir1}/log_tcp_nocomplete"
	fi


	#3. Process log_udp_complete
	if [ "$debug" -gt "0" ]; then
		echo "   Merging log_udp_complete"
	fi
	files=""
	for indir in $input_dirs ; do
		files="${files} ${dir_to_process[$indir]}/log_udp_complete"
	done
	# Print 1 header 
	head -1 "$first_input/$dir1/log_udp_complete" > "${output_dir}/${dir1}/log_udp_complete"
	# Concat the files
	if [ $udp_complete = "start" ] ; then 
		tail -q -n+2 $files | sort -n -k3 >> "${output_dir}/${dir1}/log_udp_complete"
	fi
	if [ $udp_complete = "end" ] ; then
		echo "      Not supported merging log_udp_complete by end time. Using start time"
		tail -q -n+2 $files | sort -n -k3 >> "${output_dir}/${dir1}/log_udp_complete"
	fi
	if [ $udp_complete = "concat" ] ; then 
		tail -q -n+2 $files >> "${output_dir}/${dir1}/log_udp_complete"
	fi


	#4. Process log_http_complete
	if [ "$debug" -gt "0" ]; then
		echo "   Merging log_http_complete"
	fi
	files=""
	for indir in $input_dirs ; do
		files="${files} ${dir_to_process[$indir]}/log_http_complete"
	done
	# Print 1 header 
	head -2 "$first_input/$dir1/log_http_complete" > "${output_dir}/${dir1}/log_http_complete"
	# Concat the files
	if [ $http_complete = "start" ] ; then 
		tail -q -n+3 $files | sort -n -k5 >> "${output_dir}/${dir1}/log_http_complete"
	fi
	if [ $http_complete = "end" ] ; then
		echo "      Not supported merging log_http_complete by end time. Using start time"
		tail -q -n+3 $files | sort -n -k5 >> "${output_dir}/${dir1}/log_http_complete"
	fi
	if [ $http_complete = "concat" ] ; then 
		tail -q -n+3 $files >> "${output_dir}/${dir1}/log_http_complete"
	fi



	#5. Process log_video_complete
	if [ "$debug" -gt "0" ]; then
		echo "   Merging log_video_complete"
	fi
	files=""
	for indir in $input_dirs ; do
		files="${files} ${dir_to_process[$indir]}/log_video_complete"
	done
	# Print 1 header 
	head -1 "$first_input/$dir1/log_video_complete" > "${output_dir}/${dir1}/log_video_complete"
	# Concat the files
	if [ $video_complete = "start" ] ; then 
		tail -q -n+2 $files | sort -n -k29 >> "${output_dir}/${dir1}/log_video_complete"
	fi
	if [ $video_complete = "end" ] ; then
		tail -q -n+2 $files | sort -n -k30 >> "${output_dir}/${dir1}/log_video_complete"
	fi
	if [ $video_complete = "concat" ] ; then 
		tail -q -n+2 $files >> "${output_dir}/${dir1}/log_video_complete"
	fi



	#6. Process log_mm_complete
	if [ -e "$first_input/$dir1/log_mm_complete" ]; then
		if [ "$debug" -gt "0" ]; then
			echo "   Merging log_mm_complete"
		fi
		files=""
		for indir in $input_dirs ; do
			files="${files} ${dir_to_process[$indir]}/log_mm_complete"
		done
		# Concat the files
		if [ $mm_complete = "start" ] ; then 
			echo "      Not supported merging log_mm_complete by start time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_mm_complete"
		fi
		if [ $mm_complete = "end" ] ; then
			echo "      Not supported merging log_mm_complete by end time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_mm_complete"
		fi
		if [ $mm_complete = "concat" ] ; then 
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_mm_complete"
		fi
	fi


	#7. Process log_skype_complete
	if [ -e "$first_input/$dir1/log_skype_complete" ]; then
		if [ "$debug" -gt "0" ]; then
			echo "   Merging log_skype_complete"
		fi
		files=""
		for indir in $input_dirs ; do
			files="${files} ${dir_to_process[$indir]}/log_skype_complete"
		done
		# Concat the files
		if [ $skype_complete = "start" ] ; then 
			echo "      Not supported merging log_skype_complete by start time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_skype_complete"
		fi
		if [ $skype_complete = "end" ] ; then
			echo "      Not supported merging log_skype_complete by end time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_skype_complete"
		fi
		if [ $skype_complete = "concat" ] ; then 
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_skype_complete"
		fi
	fi


	#8. Process log_chat_complete
	if [ -e "$first_input/$dir1/log_chat_complete" ]; then
		if [ "$debug" -gt "0" ]; then
			echo "   Merging log_chat_complete"
		fi
		files=""
		for indir in $input_dirs ; do
			files="${files} ${dir_to_process[$indir]}/log_chat_complete"
		done
		# Concat the files
		if [ $chat_complete = "start" ] ; then 
			echo "      Not supported merging log_chat_complete by start time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_complete"
		fi
		if [ $chat_complete = "end" ] ; then
			echo "      Not supported merging log_chat_complete by end time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_complete"
		fi
		if [ $chat_complete = "concat" ] ; then 
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_complete"
		fi
	fi


	#9. Process log_chat_messages
	if [ -e "$first_input/$dir1/log_chat_messages" ]; then
		if [ "$debug" -gt "0" ]; then
			echo "   Merging log_chat_messages"
		fi
		files=""
		for indir in $input_dirs ; do
			files="${files} ${dir_to_process[$indir]}/log_chat_messages"
		done
		# Concat the files
		if [ $chat_messages = "start" ] ; then 
			echo "      Not supported merging log_chat_messages by start time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_messages"
		fi
		if [ $chat_messages = "end" ] ; then
			echo "      Not supported merging log_chat_messages by end time. Using concatenation"
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_messages"
		fi
		if [ $chat_messages = "concat" ] ; then 
			tail -q -n+1 $files >> "${output_dir}/${dir1}/log_chat_messages"
		fi
	fi

	# Delete merged directories, if required
	if $delete_merged ; then
		for indir in $input_dirs ; do
			if [ "$debug" -gt "0" ]; then
				echo "   Removing ${dir_to_process[$indir]}"
			fi
			# Avoid unsafe deletes
			if [[ ${dir_to_process[$indir]} =~ .*/[0-9]{4}_[0-9]{2}_[0-9]{2}_[0-9]{2}_[0-9]{2}.out ]]; then
				rm -rf ${dir_to_process[$indir]}
			else
				echo "Something went wrong when deleting. Quitting..."
				exit
			fi
		done
	fi

done

