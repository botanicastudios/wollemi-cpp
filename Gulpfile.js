var gulp = require('gulp')
var shell = require('gulp-shell')
var watch = require('gulp-watch')
var spawn = require('child_process').spawn;
var exec = require('child_process').exec;
var execFile = require('child_process').execFile;

var child = null;

gulp.task('compile', function(cb) {
  if(child) { child.kill(); }
  exec('make -C ./build', function(err, stdout, stderr) {
    console.log(stdout);
    console.log(stderr);
    cb(err);
    child = spawn('./build/wollemi', { stdio: 'inherit' });
  });
});

gulp.task('watch', function() {
  gulp.watch('./source/*.cpp', ['compile']);
});

gulp.task('default', function() {
  gulp.start('compile');
  gulp.watch('./source/*.cpp', ['compile']);
});
