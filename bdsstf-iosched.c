/*
 * elevator bdsstf
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

#define NUM_QUEUES 2
#define UP 0
#define DOWN 1
#define DELAY 5

struct sstf_data {
	struct queue_struct {
		struct list_head queue_head;
		int (*compare) (sector_t first, sector_t second);
	}queue[NUM_QUEUES];
	sector_t last_sector;
	pid_t last_pid;
	struct delayed_work dw;
	unsigned long delay;
	unsigned int work_flag;
	struct request_queue *q;
};

static inline void bdsstf_delayed_work_fn(struct work_struct *work)
{
	struct sstf_data *sd;
	sd = container_of(work, struct sstf_data, dw.work);
	spin_lock(sd->q->queue_lock);
	sd->q->request_fn(sd->q);
	spin_unlock(sd->q->queue_lock);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *sd = q->elevator->elevator_data;
	unsigned int temp1, temp2, remove;
	struct request *rq[NUM_QUEUES];
       
	temp1 = list_empty(&sd->queue[UP].queue_head);
	temp2 = list_empty(&sd->queue[DOWN].queue_head);

	if(temp1 && temp2) return 0;
	if (temp1 && !temp2) {
		rq[DOWN] = list_entry(sd->queue[DOWN].queue_head.next, struct request, queuelist);
		remove = DOWN;
	}
	else if (temp2 && !temp1) {
		rq[UP] = list_entry(sd->queue[UP].queue_head.next, struct request, queuelist);
		remove = UP;
	}
	else {
		rq[DOWN] = list_entry(sd->queue[DOWN].queue_head.next, struct request, queuelist);
		rq[UP] = list_entry(sd->queue[UP].queue_head.next, struct request, queuelist);
		if((rq[UP]->sector - sd->last_sector) <= (sd->last_sector - rq[DOWN]->sector)) remove = UP;
		else remove = DOWN;
	}
	
	if((sd->work_flag == 0) && (rq[remove]->pid != sd->last_pid)) {
		sd->work_flag = 1;
		sd->q = q;
		schedule_delayed_work(&sd->dw, msecs_to_jiffies(sd->delay));
		return 0;
	}

	sd->work_flag = 0;
	sd->last_pid = rq[remove]->pid;
	list_del_init(&rq[remove]->queuelist);
	elv_dispatch_sort(q, rq[remove]);
	return 1;
}

int compare_up(sector_t first, sector_t second)
{
	if(first < second) return 1;
	if(first == second) return 0;
	return -1;
}

int compare_down(sector_t first, sector_t second)
{
	return -compare_up(first, second);
}

void insert_queue(struct queue_struct *q, struct request *rq)
{
	struct request *iterator;

	list_for_each_entry(iterator, &q->queue_head, queuelist) 
		if(q->compare(rq->sector, iterator->sector) > 0) break;
	list_add(&rq->queuelist, iterator->queuelist.prev);
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sd = q->elevator->elevator_data;

	if(rq->sector >= sd->last_sector) insert_queue(&sd->queue[UP], rq);
	else 	insert_queue(&sd->queue[DOWN], rq);

	rq->pid = task_tgid_vnr(current);
	if(sd->work_flag == 1) {
		cancel_delayed_work(&sd->dw);
		q->request_fn(q);
	}
}

static void *sstf_init_queue(struct request_queue *q)
{
	struct sstf_data *sd;

	sd = kmalloc_node(sizeof(*sd), GFP_KERNEL, q->node);
	if (!sd)
		return NULL;
	INIT_LIST_HEAD(&sd->queue[UP].queue_head);
	INIT_LIST_HEAD(&sd->queue[DOWN].queue_head);
	sd->last_sector = 0;
	sd->queue[UP].compare = compare_up;
	sd->queue[DOWN].compare = compare_down;
	sd->last_pid = current->pid;
	sd->delay = DELAY;
	sd->q = NULL;
	sd->work_flag = 0;
	INIT_DELAYED_WORK(&sd->dw, bdsstf_delayed_work_fn);
	return sd;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *sd = e->elevator_data;

	BUG_ON(!list_empty(&sd->queue[UP].queue_head) || !list_empty(&sd->queue[DOWN].queue_head));
	kfree(sd);
}

static struct elevator_type elevator_sstf = {
	.ops = {
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "bdsstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	elv_register(&elevator_sstf);
	return 0;
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_AUTHOR("TEAM VILLARREAL CF");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler");
