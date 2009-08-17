# loadbuild.mk
# Jeremy Barnes, 11 August 2009
# loadbuilding for github contest

JML_BIN := jml/../build/$(ARCH)/bin

loadbuild: results.txt fake-results.txt

results.txt: data/ranker.cls
	/usr/bin/time \
	$(BIN)/github \
		--dump-results \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@

fake-results.txt: data/ranker.cls
	/usr/bin/time \
	$(BIN)/github \
		--fake-test \
		--random-seed 2 \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@
	tail -n20 $@

data/ranker.cls: \
		data/ranker-fv.txt.gz \
		ranker-classifier-training-config.txt
	/usr/bin/time \
	$(JML_BIN)/classifier_training_tool \
		--configuration-file ranker-classifier-training-config.txt \
		--group-feature GROUP \
		--weight-spec WT/V \
		--validation-split 20 \
		--testing-split 10 \
		--randomize-order \
		--trainer-name default \
		--ignore-var WT \
		--ignore-var GROUP \
		--ignore-var REAL_TEST \
		--testing-filter 'REAL_TEST == 1' \
		-G 2 -C 2 \
		--output-file $@~ \
		--no-eval-by-group \
		$< \
	2>&1 | tee $@.log
	mv $@~ $@

data/ranker-fv.txt.gz:
	/usr/bin/time \
	$(BIN)/github \
		--dump-merger-data \
		--include-all-correct=0 \
		--num-users=20000 \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@

